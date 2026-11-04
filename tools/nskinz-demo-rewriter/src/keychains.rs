use super::*;
use std::collections::BTreeMap;

const PREFIX: &str = "m_AttributeManager.m_Item";
const VECTOR: &str = "m_AttributeManager.m_Item.m_NetworkedDynamicAttributes.m_Attributes";
const MESH_MASK: &str = "CBodyComponent.m_skeletonInstance.m_modelState.m_MeshGroupMask";
const DEFINITIONS: [u16; 7] = [299, 300, 301, 302, 306, 314, 321];
const MAX_ATTRIBUTES: usize = 4096;

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct KeychainPosition {
    modern: [f32; 3],
    legacy: [f32; 3],
}

#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct RequestKeychain {
    pub definition_index: u16,
    pub kit_id: u32,
    #[serde(default)]
    pub position: Option<KeychainPosition>,
}

impl RequestKeychain {
    pub fn validate_position(&self) -> Result<()> {
        if let Some(position) = &self.position {
            anyhow::ensure!(
                self.kit_id > 0
                    && position
                        .modern
                        .iter()
                        .chain(&position.legacy)
                        .all(|coordinate| coordinate.is_finite() && coordinate.abs() <= 128.0),
                "invalid custom keychain position"
            );
        }
        Ok(())
    }

    fn custom_position(&self, mesh_mask: u64) -> Option<[f32; 3]> {
        self.position.as_ref().map(|position| {
            if mesh_mask & 2 != 0 {
                position.legacy
            } else {
                position.modern
            }
        })
    }
}

#[derive(Clone)]
pub struct KeychainRule {
    pub steam: u64,
    pub request: RequestKeychain,
}

#[derive(Deserialize)]
struct Mount {
    position: [f32; 3],
}

#[derive(Deserialize)]
struct MountVariants {
    legacy: Option<Mount>,
    modern: Option<Mount>,
}

static MOUNTS: LazyLock<HashMap<u16, MountVariants>> = LazyLock::new(|| {
    serde_json::from_str(include_str!("keychain_mounts.json"))
        .expect("compiled, model-derived keychain mount table")
});

pub fn supported_definition(definition: u16) -> bool {
    MOUNTS.contains_key(&definition)
}

#[derive(Clone, Debug, PartialEq)]
struct Attribute {
    definition: u16,
    bits: u32,
}

fn has_keychain(attributes: &[Attribute]) -> bool {
    attributes
        .iter()
        .any(|a| a.definition == 299 && a.bits != 0)
}

fn default_mount(definition: u16, mesh_mask: u64) -> Result<[f32; 3]> {
    let variants = MOUNTS
        .get(&definition)
        .context("no model-derived keychain mount")?;
    let mount = if mesh_mask & 2 != 0 {
        variants.legacy.as_ref()
    } else {
        variants.modern.as_ref()
    };
    Ok(mount
        .or(variants.modern.as_ref())
        .or(variants.legacy.as_ref())
        .context("no keychain mount for this weapon model")?
        .position)
}

fn materialize(
    attributes: &mut Vec<Attribute>,
    kit: u32,
    position: [f32; 3],
    existing: bool,
    custom: bool,
) {
    if kit == 0 {
        // Absence is already a successful clear. Do not create an empty module
        // or inflate an originally empty vector just to encode "no keychain".
        for attribute in attributes {
            if matches!(attribute.definition, 299 | 321) {
                attribute.bits = 0;
            }
        }
        return;
    }
    let defaults = [
        kit,
        position[0].to_bits(),
        position[1].to_bits(),
        position[2].to_bits(),
        0,
        0,
        0,
    ];
    for (definition, bits) in DEFINITIONS.into_iter().zip(defaults) {
        let mut found = false;
        for attribute in attributes.iter_mut().filter(|a| a.definition == definition) {
            found = true;
            if definition == 299 || !existing || (custom && matches!(definition, 300..=302)) {
                attribute.bits = bits;
            }
        }
        if !found {
            attributes.push(Attribute { definition, bits });
        }
    }
}

pub fn fields(
    entity: &Entity,
    pending: &[(String, FieldValue)],
    rule: &KeychainRule,
) -> Result<Vec<(String, FieldValue)>> {
    // Include paint/sticker additions from this same record before allocating
    // any slots. Never append against a stale discovery-time vector length.
    let overlay: HashMap<&str, &FieldValue> =
        pending.iter().map(|(n, v)| (n.as_str(), v)).collect();
    let get = |name: &str| {
        overlay
            .get(name)
            .copied()
            .or_else(|| entity.try_get_property(name))
    };
    let count = get(VECTOR).and_then(integer).unwrap_or_default() as usize;
    anyhow::ensure!(
        count <= MAX_ATTRIBUTES,
        "keychain attribute vector exceeds {MAX_ATTRIBUTES} entries"
    );
    let mut original = Vec::with_capacity(count);
    for slot in 0..count {
        let base = format!("{VECTOR}.{slot:04}");
        let definition = get(&format!("{base}.m_iAttributeDefinitionIndex"))
            .and_then(integer)
            .context("keychain source attribute has no definition")?
            as u16;
        let bits = get(&format!("{base}.m_iRawValue32"))
            .and_then(union_u32)
            .context("keychain source attribute has no 32-bit value")?;
        original.push(Attribute { definition, bits });
    }
    let mut attributes = original.clone();
    let existing = has_keychain(&original);
    let mesh_mask = get(MESH_MASK).and_then(integer).unwrap_or(1);
    let custom = rule.request.custom_position(mesh_mask);
    let position = if let Some(position) = custom {
        position
    } else if rule.request.kit_id == 0 {
        [0.0; 3]
    } else {
        default_mount(rule.request.definition_index, mesh_mask)?
    };
    materialize(
        &mut attributes,
        rule.request.kit_id,
        position,
        existing,
        custom.is_some(),
    );
    anyhow::ensure!(
        attributes.len() <= MAX_ATTRIBUTES,
        "keychain attributes have no remaining capacity"
    );
    let mut fields = Vec::new();
    if attributes.len() != count {
        fields.push((
            VECTOR.into(),
            FieldValue::Unsigned32(attributes.len() as u32),
        ));
    }
    for (index, attribute) in attributes.iter().enumerate() {
        if index >= count {
            push_attribute(
                &mut fields,
                PREFIX,
                index,
                attribute.definition,
                f32::from_bits(attribute.bits),
            );
        } else {
            let overridden = match rule.request.kit_id {
                0 => matches!(attribute.definition, 299 | 321),
                _ => {
                    attribute.definition == 299
                        || (!existing && DEFINITIONS.contains(&attribute.definition))
                        || (custom.is_some() && matches!(attribute.definition, 300..=302))
                }
            };
            if !overridden {
                continue;
            }
            let base = format!("{VECTOR}.{index:04}");
            let initial = format!("{base}.m_flInitialValue");
            if attribute.bits == original[index].bits
                && get(&initial).and_then(union_u32) == Some(attribute.bits)
            {
                continue;
            }
            fields.push((
                format!("{base}.m_iRawValue32"),
                FieldValue::Float(f32::from_bits(attribute.bits)),
            ));
            fields.push((initial, FieldValue::Float(f32::from_bits(attribute.bits))));
        }
    }
    if rule.request.kit_id > 0 || original.iter().any(|a| a.definition == 299) {
        // A new network identity avoids reusing the original inventory item's
        // cached cosmetics. Preserve its definition, paint, stickers and name.
        let mut salt = 0x4B45_5943_4841_494E
            ^ u64::from(entity.handle())
            ^ (u64::from(rule.request.kit_id) << 32);
        if let Some(position) = custom {
            for coordinate in position {
                salt = (salt ^ u64::from(coordinate.to_bits())).wrapping_mul(0x100000001b3);
            }
        }
        let (high, low) = fnv_identity(rule.steam, rule.request.definition_index, salt);
        for (suffix, value) in [
            ("m_iItemIDHigh", FieldValue::Unsigned32(high)),
            ("m_iItemIDLow", FieldValue::Unsigned32(low)),
            ("m_bInitialized", FieldValue::Boolean(true)),
        ] {
            let name = format!("{PREFIX}.{suffix}");
            if get(&name) != Some(&value) {
                fields.push((name, value));
            }
        }
    }
    Ok(fields)
}

pub fn verify(entity: &Entity, rule: &KeychainRule) -> Result<()> {
    let count = field_integer(entity, VECTOR).unwrap_or_default() as usize;
    anyhow::ensure!(
        count <= MAX_ATTRIBUTES,
        "keychain verification vector too large"
    );
    let mut values: BTreeMap<u16, Vec<u32>> = BTreeMap::new();
    let custom = rule
        .request
        .custom_position(field_integer(entity, MESH_MASK).unwrap_or(1));
    for slot in 0..count {
        let base = format!("{VECTOR}.{slot:04}");
        let Some(definition) =
            field_integer(entity, &format!("{base}.m_iAttributeDefinitionIndex"))
        else {
            continue;
        };
        if DEFINITIONS.contains(&(definition as u16)) {
            let bits = entity
                .try_get_property(&format!("{base}.m_iRawValue32"))
                .and_then(union_u32)
                .context("keychain verification missing raw value")?;
            if let Some(position) = custom.filter(|_| matches!(definition, 300..=302)) {
                let expected = position[(definition - 300) as usize].to_bits();
                let initial = entity
                    .try_get_property(&format!("{base}.m_flInitialValue"))
                    .and_then(union_u32);
                anyhow::ensure!(
                    bits == expected && initial == Some(expected),
                    "custom keychain position attribute {definition} did not round-trip"
                );
            }
            values.entry(definition as u16).or_default().push(bits);
        }
    }
    if rule.request.kit_id > 0 {
        for definition in DEFINITIONS {
            anyhow::ensure!(
                values.contains_key(&definition),
                "missing keychain attribute {definition}"
            );
        }
    }
    for value in values.get(&299).into_iter().flatten() {
        anyhow::ensure!(
            *value == rule.request.kit_id,
            "keychain ID bits: expected {}, observed {}",
            rule.request.kit_id,
            value
        );
    }
    if rule.request.kit_id == 0 {
        anyhow::ensure!(
            values.get(&321).into_iter().flatten().all(|v| *v == 0),
            "cleared keychain still has an attached sticker"
        );
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn replacement_preserves_source_position_seed_and_unrelated_fields() {
        let mut attributes = vec![
            Attribute {
                definition: 6,
                bits: 302.0f32.to_bits(),
            },
            Attribute {
                definition: 299,
                bits: 17,
            },
            Attribute {
                definition: 300,
                bits: 23.927616f32.to_bits(),
            },
            Attribute {
                definition: 301,
                bits: 0.39287868f32.to_bits(),
            },
            Attribute {
                definition: 302,
                bits: 2.2566724f32.to_bits(),
            },
            Attribute {
                definition: 306,
                bits: 80810,
            },
            Attribute {
                definition: 113,
                bits: 76,
            },
        ];
        let before = attributes.clone();
        materialize(&mut attributes, 34, [1.0, 2.0, 3.0], true, false);
        for (index, source) in before.iter().enumerate() {
            assert_eq!(
                attributes[index].bits,
                if source.definition == 299 {
                    34
                } else {
                    source.bits
                }
            );
        }
        assert_ne!(attributes[1].bits, 34.0f32.to_bits());
    }

    #[test]
    fn creation_appends_after_large_vectors_and_clear_does_not_create() {
        let mut attributes: Vec<_> = (1000..1100)
            .map(|definition| Attribute {
                definition,
                bits: 12,
            })
            .collect();
        materialize(&mut attributes, 17, [4.0, 5.0, 6.0], false, false);
        assert_eq!(attributes.len(), 107);
        assert_eq!(attributes[100].definition, 299);
        assert_eq!(attributes[100].bits, 17);
        assert_eq!(attributes[101].bits, 4.0f32.to_bits());
        materialize(&mut attributes, 0, [0.0; 3], true, false);
        assert_eq!(attributes.len(), 107);
        assert_eq!(attributes[100].bits, 0);
        let mut empty = Vec::new();
        materialize(&mut empty, 0, [0.0; 3], false, false);
        assert!(empty.is_empty());
    }

    #[test]
    fn creation_reinitializes_a_vacant_slot_with_stale_values() {
        let mut attributes: Vec<_> = DEFINITIONS
            .into_iter()
            .zip([0, 42, 43, 44, 120, 1, 76])
            .map(|(definition, bits)| Attribute { definition, bits })
            .collect();
        assert!(!has_keychain(&attributes));
        materialize(&mut attributes, 17, [4.0, 5.0, 6.0], false, false);
        assert_eq!(
            attributes.iter().map(|a| a.bits).collect::<Vec<_>>(),
            [
                17,
                4.0f32.to_bits(),
                5.0f32.to_bits(),
                6.0f32.to_bits(),
                0,
                0,
                0
            ]
        );
    }

    #[test]
    fn compiled_default_mounts_cover_every_supported_firearm() {
        assert_eq!(MOUNTS.len(), 34);
        for variants in MOUNTS.values() {
            for mount in [variants.legacy.as_ref(), variants.modern.as_ref()]
                .into_iter()
                .flatten()
            {
                assert!(mount.position.iter().all(|v| v.is_finite()));
                assert!(mount.position.iter().any(|v| v.abs() > 0.01));
            }
        }
    }

    fn read_attributes(entity: &Entity) -> Vec<Attribute> {
        let count = field_integer(entity, VECTOR).unwrap_or_default() as usize;
        (0..count)
            .map(|slot| {
                let base = format!("{VECTOR}.{slot:04}");
                Attribute {
                    definition: field_integer(
                        entity,
                        &format!("{base}.m_iAttributeDefinitionIndex"),
                    )
                    .unwrap() as u16,
                    bits: entity
                        .try_get_property(&format!("{base}.m_iRawValue32"))
                        .and_then(union_u32)
                        .unwrap(),
                }
            })
            .collect()
    }

    #[test]
    #[ignore = "requires NSKINZ_DEFAULT_GLOVE_MATRIX with independent source demos"]
    fn real_demos_keychain_add_replace_clear_and_seek_preserve_other_cosmetics() {
        // Two independent Parser instances contain large entity arrays. Keep
        // this test's stack separate from the platform's small test-thread stack.
        std::thread::Builder::new()
            .stack_size(16 * 1024 * 1024)
            .spawn(run_real_demo_matrix)
            .unwrap()
            .join()
            .unwrap();
    }

    fn run_real_demo_matrix() {
        #[derive(Deserialize)]
        struct Fixture {
            input_dem: PathBuf,
        }
        let fixtures: Vec<Fixture> = serde_json::from_slice(
            &std::fs::read(
                std::env::var_os("NSKINZ_DEFAULT_GLOVE_MATRIX").expect("set matrix path"),
            )
            .unwrap(),
        )
        .unwrap();
        assert!(fixtures.len() >= 3);
        let directory = PathBuf::from(
            std::env::var_os("NSKINZ_KEYCHAIN_TEST_OUTPUT").expect("set output directory"),
        );
        std::fs::create_dir_all(&directory).unwrap();
        for (index, fixture) in fixtures.into_iter().enumerate() {
            let discovery = discover(&fixture.input_dem).unwrap();
            let accounts: BTreeSet<_> = discovery.controller_steam.values().copied().collect();
            let request = Request {
                schema_version: 2,
                input_dem: fixture.input_dem.clone(),
                output_dem: directory.join(format!("matrix-{index}-keychains.dem")),
                cs2_game_dir: None,
                passes: accounts
                    .into_iter()
                    .map(|steam| RequestPass {
                        steam_id64: steam.to_string(),
                        items: Vec::new(),
                        keychains: MOUNTS
                            .keys()
                            .map(|definition| RequestKeychain {
                                definition_index: *definition,
                                kit_id: 17,
                                position: None,
                            })
                            .collect(),
                    })
                    .collect(),
            };
            let plan =
                build_rewrite_plan(&parse_plans(&request).unwrap(), &discovery, None).unwrap();
            let response = rewrite(&request).unwrap();
            assert!(
                response.keychain_weapon_count > 0 && response.verified_keychain_created_count > 0
            );
            let mut source =
                Parser::from_reader(BufReader::new(File::open(&request.input_dem).unwrap()))
                    .unwrap();
            source.register_observer::<Discovery>();
            let mut rewritten =
                Parser::from_reader(BufReader::new(File::open(&request.output_dem).unwrap()))
                    .unwrap();
            rewritten.register_observer::<Discovery>();
            let mut checked = 0;
            let mut source_existing = 0;
            let mut source_absent = 0;
            // Independent forward/backward seeks, not a continuation of the
            // writer's state. Include early spawns and late-round checkpoints.
            for tick in [40000, 10000, 70000, 1000, 30000] {
                source.jump_to_tick(tick).unwrap();
                rewritten.jump_to_tick(tick).unwrap();
                for before in source.context().entities().iter() {
                    let Some(rule) = plan.keychain_weapons.get(&before.handle()) else {
                        continue;
                    };
                    let after = rewritten
                        .context()
                        .entities()
                        .get_by_handle(before.handle() as usize)
                        .unwrap();
                    verify(after, rule).unwrap();
                    let original = read_attributes(before);
                    let actual = read_attributes(after);
                    let existing = has_keychain(&original);
                    if existing {
                        source_existing += 1;
                    } else {
                        source_absent += 1;
                        let position = default_mount(
                            rule.request.definition_index,
                            field_integer(before, MESH_MASK).unwrap_or(1),
                        )
                        .unwrap();
                        let expected = [
                            rule.request.kit_id,
                            position[0].to_bits(),
                            position[1].to_bits(),
                            position[2].to_bits(),
                            0,
                            0,
                            0,
                        ];
                        for (definition, bits) in DEFINITIONS.into_iter().zip(expected) {
                            assert!(actual.contains(&Attribute { definition, bits }),
                                "default mount mismatch at tick {tick}, handle {}, attribute {definition}",
                                before.handle());
                        }
                    }
                    for attribute in &original {
                        if attribute.definition == 299
                            || (!existing && DEFINITIONS.contains(&attribute.definition))
                        {
                            continue;
                        }
                        assert!(
                            actual.contains(attribute),
                            "changed unrequested attribute {} at tick {tick}, handle {}",
                            attribute.definition,
                            before.handle()
                        );
                    }
                    for suffix in [
                        "m_iItemDefinitionIndex",
                        "m_iEntityQuality",
                        "m_szCustomName",
                    ] {
                        let field = format!("{PREFIX}.{suffix}");
                        assert_eq!(
                            before.try_get_property(&field),
                            after.try_get_property(&field)
                        );
                    }
                    checked += 1;
                }
            }
            assert!(checked > 0 && source_absent > 0);
            drop(source);
            drop(rewritten);
            let mut clear = request;
            clear.input_dem = clear.output_dem;
            clear.output_dem = directory.join(format!("matrix-{index}-clear.dem"));
            for pass in &mut clear.passes {
                for keychain in &mut pass.keychains {
                    keychain.kit_id = 0;
                }
            }
            let cleared = rewrite(&clear).unwrap();
            assert_eq!(
                cleared.keychain_weapon_count,
                response.keychain_weapon_count
            );
            eprintln!("keychain matrix {index}: weapons={}, records={}, Created={}, seek_checks={checked}, source_existing={source_existing}, source_absent={source_absent}, clear_records={}",
                response.keychain_weapon_count, response.verified_keychain_record_count, response.verified_keychain_created_count, cleared.verified_keychain_record_count);
        }
    }
}
