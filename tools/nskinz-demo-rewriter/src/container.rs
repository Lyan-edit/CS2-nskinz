//! Engine-facing PBDEMS2 indexes, checked independently of entity decoding.
use anyhow::{bail, ensure, Context, Result};
use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::Path;

#[derive(Debug)]
pub(crate) struct HeaderIndexes {
    pub declared: [u32; 2],
    pub actual: [Option<u64>; 2],
}

fn read_var_u32(reader: &mut impl Read, position: &mut u64) -> Result<u32> {
    let mut value = 0;
    for shift in (0..35).step_by(7) {
        let mut byte = [0];
        reader.read_exact(&mut byte).context("truncated Demo command header")?;
        *position += 1;
        ensure!(shift != 28 || byte[0] <= 15, "Demo command varint exceeds u32");
        value |= u32::from(byte[0] & 127) << shift;
        if byte[0] < 128 { return Ok(value); }
    }
    bail!("invalid Demo command varint")
}

pub(crate) fn inspect_header_indexes(reader: &mut (impl Read + Seek)) -> Result<HeaderIndexes> {
    let length = reader.seek(SeekFrom::End(0))?;
    reader.seek(SeekFrom::Start(0))?;
    let mut header = [0; 16];
    reader.read_exact(&mut header).context("truncated Demo file header")?;
    ensure!(&header[..8] == b"PBDEMS2\0", "not a PBDEMS2 Demo");
    let declared = [u32::from_le_bytes(header[8..12].try_into()?),
        u32::from_le_bytes(header[12..16].try_into()?)];
    let mut actual = [None; 2];
    let mut position = 16;
    // Do not stop at DemStop: resource groups and FileInfo can follow it.
    while position < length {
        let start = position;
        let command = read_var_u32(reader, &mut position)? & !64;
        let _tick = read_var_u32(reader, &mut position)?;
        let size = read_var_u32(reader, &mut position)?;
        let end = position + u64::from(size);
        ensure!(end <= length, "Demo command at {start} exceeds file length");
        if command == 2 && actual[0].is_none() { actual[0] = Some(start); }
        if command == 15 && actual[1].is_none() { actual[1] = Some(start); }
        reader.seek_relative(i64::from(size))?;
        position = end;
    }
    Ok(HeaderIndexes { declared, actual })
}

impl HeaderIndexes {
    pub fn verify(&self) -> Result<()> {
        ensure!(self.actual[0].is_some(), "Demo is missing FileInfo");
        for (index, name) in ["FileInfo", "SpawnGroups"].into_iter().enumerate() {
            match self.actual[index] {
                Some(actual) => ensure!(actual == u64::from(self.declared[index]),
                    "Demo {name} header index is stale: declared {}, actual {actual}", self.declared[index]),
                None => ensure!(matches!(self.declared[index], 0 | u32::MAX),
                    "Demo {name} header index references a missing command: {}", self.declared[index]),
            }
        }
        Ok(())
    }
}

pub(crate) fn verify_header_indexes(path: &Path) -> Result<()> {
    let file = File::open(path)?;
    let mut reader = BufReader::with_capacity(8 * 1024 * 1024, file);
    inspect_header_indexes(&mut reader)?.verify()
        .with_context(|| format!("verify engine Demo header indexes: {}", path.display()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use source2_demo::prelude::Context;
    use source2_demo::writer::{DemoRewriter, DemoWriter, MessageRewrite, RewriteInterests};
    use source2_demo::proto::{CDemoFileInfo, EDemoCommands, Message};
    use source2_demo::error::ParserError;
    use source2_demo::writer::write_demo_message_with_compression;
    use std::io::Cursor;

    fn writer_stack(test: impl FnOnce() + Send + 'static) {
        // Match the production rewrite worker's stack for parser fixtures.
        std::thread::Builder::new().stack_size(32 * 1024 * 1024)
            .spawn(test).unwrap().join().unwrap();
    }

    fn fixture(spawn: bool, compressed: bool) -> Vec<u8> {
        let mut bytes = b"PBDEMS2\0".to_vec();
        bytes.extend_from_slice(&[0; 8]);
        write_demo_message_with_compression(&mut bytes, EDemoCommands::DemConsoleCmd, 0, &[8, 1], false).unwrap();
        write_demo_message_with_compression(&mut bytes, EDemoCommands::DemStop, 10, &[], false).unwrap();
        if spawn {
            let offset = (bytes.len() as u32).to_le_bytes();
            bytes[12..16].copy_from_slice(&offset);
            write_demo_message_with_compression(&mut bytes, EDemoCommands::DemSpawnGroups, 10, &[26, 0], compressed).unwrap();
        }
        let offset = (bytes.len() as u32).to_le_bytes();
        bytes[8..12].copy_from_slice(&offset);
        write_demo_message_with_compression(&mut bytes, EDemoCommands::DemFileInfo, 10,
            &CDemoFileInfo::default().encode_to_vec(), false).unwrap();
        bytes
    }

    struct Resize { length: usize, drop_spawn: bool }
    impl DemoRewriter for Resize {
        fn interests(&self) -> RewriteInterests { RewriteInterests::DEMO_MESSAGE }
        fn rewrite_demo_message(&mut self, _: &Context, _: u32, kind: EDemoCommands, _: &[u8])
            -> std::result::Result<MessageRewrite, ParserError> {
            Ok(if kind == EDemoCommands::DemConsoleCmd { MessageRewrite::Replace(vec![0; self.length]) }
                else if kind == EDemoCommands::DemSpawnGroups && self.drop_spawn { MessageRewrite::Drop }
                else { MessageRewrite::Keep })
        }
    }

    #[test]
    fn writer_relocates_both_indexes_after_growth_and_shrink() {
        writer_stack(|| {
        for length in [0, 1, 128, 4096] {
            for compressed in [false, true] {
                let source = fixture(true, compressed);
                let mut output = Cursor::new(Vec::new());
                let mut writer = DemoWriter::from_slice(&source, &mut output).unwrap();
                writer.add_rewriter(Resize { length, drop_spawn: false });
                writer.run().unwrap(); drop(writer);
                let indexes = inspect_header_indexes(&mut output).unwrap();
                indexes.verify().unwrap();
                assert_ne!(&source[12..16], &output.get_ref()[12..16]);
            }
        }
        });
    }

    #[test]
    fn raw_passthrough_preserves_indexes_and_absent_spawn_is_supported() {
        writer_stack(|| {
        for spawn in [false, true] {
            for compressed in [false, true] {
                let source = fixture(spawn, compressed);
                let mut output = Cursor::new(Vec::new());
                DemoWriter::from_slice(&source, &mut output).unwrap().run().unwrap();
                assert_eq!(&source, output.get_ref());
                inspect_header_indexes(&mut output).unwrap().verify().unwrap();
            }
        }
        });
    }

    #[test]
    fn dropped_spawn_clears_its_old_header_index() {
        writer_stack(|| {
        let source = fixture(true, false);
        let mut output = Cursor::new(Vec::new());
        let mut writer = DemoWriter::from_slice(&source, &mut output).unwrap();
        writer.add_rewriter(Resize { length: 128, drop_spawn: true });
        writer.run().unwrap(); drop(writer);
        assert_eq!(&output.get_ref()[12..16], &[0; 4]);
        inspect_header_indexes(&mut output).unwrap().verify().unwrap();
        });
    }

    #[test]
    fn independent_guard_rejects_stale_interior_and_truncated_indexes() {
        let valid = fixture(true, false);
        for field in [8, 12] {
            let mut corrupt = valid.clone();
            corrupt[field..field+4].copy_from_slice(&17u32.to_le_bytes());
            assert!(inspect_header_indexes(&mut Cursor::new(corrupt)).unwrap().verify().is_err());
        }
        let mut short = valid.clone(); short.pop();
        assert!(inspect_header_indexes(&mut Cursor::new(short)).is_err());
        let mut extra = valid; extra.push(128);
        assert!(inspect_header_indexes(&mut Cursor::new(extra)).is_err());
    }

    #[test]
    #[ignore = "requires NSKINZ_HEADER_MATRIX with independent real Demo paths and expected validity"]
    fn real_demos_have_engine_header_indexes() {
        let file = std::env::var_os("NSKINZ_HEADER_MATRIX").expect("header fixture matrix");
        let cases: Vec<serde_json::Value> = serde_json::from_slice(&std::fs::read(file).unwrap()).unwrap();
        for case in cases {
            let path = Path::new(case["path"].as_str().unwrap());
            let result = verify_header_indexes(path);
            assert_eq!(result.is_ok(), case["valid"].as_bool().unwrap(), "{}: {result:?}", path.display());
            println!("header_index_fixture={} expected_valid={} result={result:?}", path.display(), case["valid"]);
        }
    }
}
