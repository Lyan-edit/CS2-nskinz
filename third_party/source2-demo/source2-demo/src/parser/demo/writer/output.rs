use crate::error::ParserError;
use crate::parser::demo::writer::{DemoWriter, RawDemoMessage};
use crate::proto::EDemoCommands;
use crate::reader::{BitsReader, MessageReader};
use crate::writer::{write_var_u32_to_buf, MessageWriter};
use std::io::{Seek, SeekFrom, Write};

impl<'a, R, W> DemoWriter<'a, R, W>
where
    R: BitsReader + MessageReader,
    W: Write + Seek,
{
    pub(crate) fn write_demo_message_with_compression(
        &mut self,
        msg_type: EDemoCommands,
        tick: u32,
        payload: &[u8],
        compressed: bool,
    ) -> Result<(), ParserError> {
        if msg_type == EDemoCommands::DemFileInfo && self.file_info_offset.is_none() {
            self.file_info_offset = Some(self.bytes_written);
        }
        if msg_type == EDemoCommands::DemSpawnGroups && self.spawn_groups_offset.is_none() {
            self.spawn_groups_offset = Some(self.bytes_written);
        }
        self.bytes_written += self
            .writer
            .write_message_with_compression(msg_type, tick, payload, compressed)?
            as u64;
        Ok(())
    }

    pub(crate) fn write_raw_demo_message(
        &mut self,
        msg_type: EDemoCommands,
        tick: u32,
        raw_payload: &[u8],
        compressed: bool,
    ) -> Result<(), ParserError> {
        if msg_type == EDemoCommands::DemFileInfo && self.file_info_offset.is_none() {
            self.file_info_offset = Some(self.bytes_written);
        }
        if msg_type == EDemoCommands::DemSpawnGroups && self.spawn_groups_offset.is_none() {
            self.spawn_groups_offset = Some(self.bytes_written);
        }

        let cmd = if compressed {
            msg_type as i32 | EDemoCommands::DemIsCompressed as i32
        } else {
            msg_type as i32
        };
        let mut header = Vec::with_capacity(20);
        write_var_u32_to_buf(&mut header, cmd as u64);
        write_var_u32_to_buf(&mut header, tick as u64);
        write_var_u32_to_buf(&mut header, raw_payload.len() as u64);
        self.writer.write_all(&header)?;
        self.writer.write_all(raw_payload)?;
        self.bytes_written += (header.len() + raw_payload.len()) as u64;
        Ok(())
    }

    pub(crate) fn write_message_or_raw(
        &mut self,
        message: &RawDemoMessage,
        payload: Option<&[u8]>,
    ) -> Result<(), ParserError> {
        if let Some(payload) = payload {
            self.write_demo_message_with_compression(
                message.msg_type,
                message.tick,
                payload,
                message.compressed,
            )
        } else {
            self.write_raw_demo_message(
                message.msg_type,
                message.tick,
                message.raw_payload.as_slice(),
                message.compressed,
            )
        }
    }

    pub(crate) fn finalize_header_offsets(&mut self) -> Result<(), ParserError> {
        // PBDEMS2 stores both FileInfo and SpawnGroups file positions. Copying
        // the source's second index leaves it inside an unrelated packet after
        // entity rewrites change the stream length. Sequential parsers can
        // still accept that file while the engine cannot load scene resources.
        let file_info: u32 = self.file_info_offset.unwrap_or(0)
            .try_into()
            .map_err(|_| ParserError::ReplayEncodingError)?;
        let spawn_groups: u32 = self.spawn_groups_offset.unwrap_or(0)
            .try_into()
            .map_err(|_| ParserError::ReplayEncodingError)?;
        self.writer.seek(SeekFrom::Start(8))?;
        self.writer.write_all(&file_info.to_le_bytes())?;
        self.writer.write_all(&spawn_groups.to_le_bytes())?;
        self.writer.seek(SeekFrom::End(0))?;
        Ok(())
    }
}
