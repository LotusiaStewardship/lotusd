use std::convert::TryInto;

use serde::Deserialize;
use thiserror::Error;

/// Errors that can occur during block parsing
#[derive(Debug, Error)]
pub enum BlockParseError {
    #[error("Invalid block hex encoding: {0}")]
    InvalidBlockHex(String),
    
    #[error("Invalid target hex encoding: {0}")]
    InvalidTargetHex(String),
    
    #[error("Target must be exactly 32 bytes, got {0} bytes")]
    InvalidTargetLength(usize),
    
    #[error("Block too short: expected at least 160 bytes for header, got {0} bytes")]
    BlockTooShort(usize),
}

#[derive(Clone)]
pub struct Block {
    pub header: [u8; 160],
    pub body: Vec<u8>,
    pub target: [u8; 32],
}

#[derive(Deserialize, Debug, Clone)]
pub struct GetRawUnsolvedBlockResponse {
    pub result: Option<RawUnsolvedBlockAndTarget>,
    pub error: Option<String>,
}

#[derive(Deserialize, Debug, Clone)]
pub struct RawUnsolvedBlockAndTarget {
    pub blockhex: String,
    pub target: String,
}

/// Creates a Block from the raw unsolved block data received from the node.
/// 
/// # Errors
/// Returns an error if:
/// - The block hex is invalid
/// - The target hex is invalid  
/// - The target is not exactly 32 bytes
/// - The block data is shorter than the required 160-byte header
pub fn create_block(unsolved_block_and_target: &RawUnsolvedBlockAndTarget) -> Result<Block, BlockParseError> {
    let block = hex::decode(&unsolved_block_and_target.blockhex)
        .map_err(|e| BlockParseError::InvalidBlockHex(e.to_string()))?;
    
    if block.len() < 160 {
        return Err(BlockParseError::BlockTooShort(block.len()));
    }
    
    let target_bytes = hex::decode(&unsolved_block_and_target.target)
        .map_err(|e| BlockParseError::InvalidTargetHex(e.to_string()))?;
    
    if target_bytes.len() != 32 {
        return Err(BlockParseError::InvalidTargetLength(target_bytes.len()));
    }
    
    let mut target: [u8; 32] = target_bytes
        .try_into()
        .expect("Length already validated as 32 bytes");
    target.reverse();
    
    Ok(Block {
        header: block[0..160]
            .try_into()
            .expect("Length already validated as >= 160 bytes"),
        body: block[160..].to_vec(),
        target,
    })
}

/// Legacy function for backward compatibility - panics on invalid input.
/// Prefer using `create_block()` which returns a Result.
#[allow(dead_code)]
#[deprecated(note = "Use create_block() which returns a Result for proper error handling")]
pub fn create_block_unchecked(unsolved_block_and_target: &RawUnsolvedBlockAndTarget) -> Block {
    create_block(unsolved_block_and_target)
        .expect("Failed to parse block data from node")
}

impl Block {
    pub fn prev_hash(&self) -> &[u8] {
        &self.header[..32]
    }
    
    pub fn empty() -> Self {
        Block {
            header: [0; 160],
            body: Vec::new(),
            target: [0; 32],
        }
    }
    
    pub fn body_size(&self) -> usize {
        self.body.len()
    }
    
    pub fn get_body(&self) -> &[u8] {
        &self.body
    }
}
