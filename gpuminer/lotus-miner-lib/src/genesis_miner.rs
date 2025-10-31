// Genesis block miner for Lotus
// This module implements genesis block construction and mining
// following the SOLE REFERENCE IMPLEMENTATION in lotusd/src/chainparams.cpp

use sha2::{Sha256, Digest};
use std::time::{SystemTime, UNIX_EPOCH};

// Constants from lotusd (src/amount.h and src/consensus/consensus.h)
const SATOSHI: i64 = 1;
const LOTUS: i64 = 1_000_000 * SATOSHI;
const SUBSIDY: i64 = 260 * LOTUS;

// COINBASE_PREFIX from lotusd src/consensus/consensus.h
// static const std::vector<uint8_t> COINBASE_PREFIX = {0x6c, 0x6f, 0x67, 0x6f, 0x73};
const COINBASE_PREFIX: &[u8] = &[0x6c, 0x6f, 0x67, 0x6f, 0x73]; // "logos"

// Script opcodes from lotusd src/script/script.h
const OP_RETURN: u8 = 0x6a;
const OP_CHECKSIG: u8 = 0xac;

// Genesis script sig from lotusd src/chainparams.cpp
const GENESIS_SCRIPT_SIG: &str = "John 1:1 In the beginning was the Logos";

// Genesis coinbase output addresses from lotusd src/chainparams.cpp
const GENESIS_OUTPUT_0_HASH: &str = "ffe330c4b7643e554c62adcbe0b80537435d888b5c33d5e29a70cdd743e3a093";
const GENESIS_OUTPUT_1_PUBKEY: &str = "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f";

/// Represents a transaction input (CTxIn in lotusd)
#[derive(Clone)]
struct TxIn {
    prevout_hash: [u8; 32],
    prevout_n: u32,
    script_sig: Vec<u8>,
    sequence: u32,
}

/// Represents a transaction output (CTxOut in lotusd)
#[derive(Clone)]
struct TxOut {
    value: i64,  // Amount in satoshis
    script_pubkey: Vec<u8>,
}

/// Represents a transaction (CMutableTransaction in lotusd)
#[derive(Clone)]
struct Transaction {
    version: i32,
    vin: Vec<TxIn>,
    vout: Vec<TxOut>,
    lock_time: u32,
}

/// Represents a genesis block
pub struct GenesisBlock {
    pub header: [u8; 160],
    pub body: Vec<u8>,
    pub target: [u8; 32],
}

impl Transaction {
    /// Serialize a transaction following lotusd serialization format
    /// Format from src/primitives/transaction.h:
    /// - int32_t nVersion
    /// - std::vector<CTxIn> vin
    /// - std::vector<CTxOut> vout
    /// - uint32_t nLockTime
    fn serialize(&self) -> Vec<u8> {
        let mut data = Vec::new();
        
        // nVersion (4 bytes, little endian)
        data.extend_from_slice(&self.version.to_le_bytes());
        
        // vin count (compact size)
        data.extend_from_slice(&encode_compact_size(self.vin.len()));
        
        // Serialize each input
        for input in &self.vin {
            // prevout.hash (32 bytes)
            data.extend_from_slice(&input.prevout_hash);
            // prevout.n (4 bytes, little endian)
            data.extend_from_slice(&input.prevout_n.to_le_bytes());
            // scriptSig length (compact size)
            data.extend_from_slice(&encode_compact_size(input.script_sig.len()));
            // scriptSig
            data.extend_from_slice(&input.script_sig);
            // nSequence (4 bytes, little endian)
            data.extend_from_slice(&input.sequence.to_le_bytes());
        }
        
        // vout count (compact size)
        data.extend_from_slice(&encode_compact_size(self.vout.len()));
        
        // Serialize each output
        for output in &self.vout {
            // nValue (8 bytes, little endian)
            data.extend_from_slice(&output.value.to_le_bytes());
            // scriptPubKey length (compact size)
            data.extend_from_slice(&encode_compact_size(output.script_pubkey.len()));
            // scriptPubKey
            data.extend_from_slice(&output.script_pubkey);
        }
        
        // nLockTime (4 bytes, little endian)
        data.extend_from_slice(&self.lock_time.to_le_bytes());
        
        data
    }
    
    /// Compute transaction hash (double SHA256)
    fn get_hash(&self) -> [u8; 32] {
        let serialized = self.serialize();
        let hash1 = Sha256::digest(&serialized);
        let hash2 = Sha256::digest(&hash1);
        let mut result = [0u8; 32];
        result.copy_from_slice(&hash2);
        result
    }
    
    /// Compute transaction ID (same as hash for version 1)
    fn get_id(&self) -> [u8; 32] {
        self.get_hash()
    }
}

/// Encode a size as a Bitcoin compact size integer
fn encode_compact_size(size: usize) -> Vec<u8> {
    if size < 0xfd {
        vec![size as u8]
    } else if size <= 0xffff {
        let mut data = vec![0xfd];
        data.extend_from_slice(&(size as u16).to_le_bytes());
        data
    } else if size <= 0xffffffff {
        let mut data = vec![0xfe];
        data.extend_from_slice(&(size as u32).to_le_bytes());
        data
    } else {
        let mut data = vec![0xff];
        data.extend_from_slice(&(size as u64).to_le_bytes());
        data
    }
}

/// Build a CScript for the genesis coinbase output 0
/// From chainparams.cpp:
/// txNew.vout[0].scriptPubKey =
///     CScript() << OP_RETURN << COINBASE_PREFIX << nHeight
///               << ParseHex("ffe330c4b7643e554c62adcbe0b80537435d888b5c33d5e29a70cdd743e3a093");
fn build_genesis_output_0_script(height: i32) -> Vec<u8> {
    let mut script = Vec::new();
    
    // OP_RETURN
    script.push(OP_RETURN);
    
    // COINBASE_PREFIX (5 bytes, push as data)
    script.push(COINBASE_PREFIX.len() as u8);
    script.extend_from_slice(COINBASE_PREFIX);
    
    // nHeight (encoded as compact integer)
    if height == 0 {
        script.push(0x00); // OP_0 for height 0
    } else {
        // For non-zero heights, encode as minimal integer
        let height_bytes = encode_script_int(height);
        script.push(height_bytes.len() as u8);
        script.extend_from_slice(&height_bytes);
    }
    
    // Address hash (32 bytes)
    let hash = hex::decode(GENESIS_OUTPUT_0_HASH).expect("Invalid genesis output 0 hash");
    script.push(hash.len() as u8);
    script.extend_from_slice(&hash);
    
    script
}

/// Build a CScript for the genesis coinbase output 1
/// From chainparams.cpp:
/// txNew.vout[1].scriptPubKey =
///     CScript() << ParseHex("04678afdb0fe...") << OP_CHECKSIG;
fn build_genesis_output_1_script() -> Vec<u8> {
    let mut script = Vec::new();
    
    // Public key (65 bytes)
    let pubkey = hex::decode(GENESIS_OUTPUT_1_PUBKEY).expect("Invalid genesis output 1 pubkey");
    script.push(pubkey.len() as u8);
    script.extend_from_slice(&pubkey);
    
    // OP_CHECKSIG
    script.push(OP_CHECKSIG);
    
    script
}

/// Encode an integer as a Bitcoin script number (minimal encoding)
fn encode_script_int(value: i32) -> Vec<u8> {
    if value == 0 {
        return vec![];
    }
    
    let mut result = Vec::new();
    let negative = value < 0;
    let mut abs_value = value.abs() as u32;
    
    while abs_value > 0 {
        result.push((abs_value & 0xff) as u8);
        abs_value >>= 8;
    }
    
    // If the most significant bit is set, add an extra byte for the sign
    if result.last().unwrap() & 0x80 != 0 {
        result.push(if negative { 0x80 } else { 0x00 });
    } else if negative {
        *result.last_mut().unwrap() |= 0x80;
    }
    
    result
}

/// Create the genesis transaction
/// This mirrors CreateGenesisBlock in lotusd/src/chainparams.cpp
fn create_genesis_transaction() -> Transaction {
    // Create the coinbase input
    let script_sig_data = GENESIS_SCRIPT_SIG.as_bytes().to_vec();
    let mut script_sig = Vec::new();
    script_sig.push(script_sig_data.len() as u8);
    script_sig.extend_from_slice(&script_sig_data);
    
    let coinbase_input = TxIn {
        prevout_hash: [0u8; 32], // Null hash for coinbase
        prevout_n: 0xffffffff,   // NULL_INDEX for coinbase
        script_sig,
        sequence: 0xffffffff,    // SEQUENCE_FINAL
    };
    
    // Create the two outputs
    let output_0 = TxOut {
        value: SUBSIDY / 2,
        script_pubkey: build_genesis_output_0_script(0),
    };
    
    let output_1 = TxOut {
        value: SUBSIDY / 2,
        script_pubkey: build_genesis_output_1_script(),
    };
    
    Transaction {
        version: 1,
        vin: vec![coinbase_input],
        vout: vec![output_0, output_1],
        lock_time: 0,
    }
}

/// Compute the merkle root of transactions in a block
/// From lotusd/src/consensus/merkle.cpp:
/// For each tx, create a leaf: Hash(tx.GetHash() || tx.GetId())
/// Then compute the merkle tree
fn compute_merkle_root(txs: &[Transaction]) -> [u8; 32] {
    if txs.is_empty() {
        return [0u8; 32];
    }
    
    // Create leaves: Hash(tx.GetHash() || tx.GetId())
    // Note: In lotusd, uint256 hashes are serialized in little-endian (reversed from hash output)
    let mut hashes: Vec<[u8; 32]> = txs.iter().map(|tx| {
        let mut leaf_data = Vec::new();
        // Reverse transaction hashes to match lotusd's uint256 serialization format
        let mut tx_hash = tx.get_hash();
        tx_hash.reverse();
        let mut tx_id = tx.get_id();
        tx_id.reverse();
        leaf_data.extend_from_slice(&tx_hash);
        leaf_data.extend_from_slice(&tx_id);
        
        let hash = Sha256::digest(&Sha256::digest(&leaf_data));
        let mut result = [0u8; 32];
        result.copy_from_slice(&hash);
        result
    }).collect();
    
    // Build merkle tree
    while hashes.len() > 1 {
        let mut next_level = Vec::new();
        
        // Process pairs
        for i in (0..hashes.len()).step_by(2) {
            if i + 1 < hashes.len() {
                // Hash pair
                let mut pair_data = Vec::new();
                pair_data.extend_from_slice(&hashes[i]);
                pair_data.extend_from_slice(&hashes[i + 1]);
                let hash = Sha256::digest(&Sha256::digest(&pair_data));
                let mut result = [0u8; 32];
                result.copy_from_slice(&hash);
                next_level.push(result);
            } else {
                // Odd one out, pair with null hash
                let mut pair_data = Vec::new();
                pair_data.extend_from_slice(&hashes[i]);
                pair_data.extend_from_slice(&[0u8; 32]);
                let hash = Sha256::digest(&Sha256::digest(&pair_data));
                let mut result = [0u8; 32];
                result.copy_from_slice(&hash);
                next_level.push(result);
            }
        }
        
        hashes = next_level;
    }
    
    hashes[0]
}

/// Serialize extended metadata (empty for genesis block)
fn serialize_extended_metadata() -> Vec<u8> {
    // Empty metadata for genesis block
    // Compact size 0
    vec![0x00]
}

/// Compute hash of serialized data using double SHA256
fn compute_serialize_hash(data: &[u8]) -> [u8; 32] {
    let hash1 = Sha256::digest(data);
    let hash2 = Sha256::digest(&hash1);
    let mut result = [0u8; 32];
    result.copy_from_slice(&hash2);
    result
}

/// Serialize block body (metadata + transactions)
fn serialize_block_body(metadata: &[u8], txs: &[Transaction]) -> Vec<u8> {
    let mut body = Vec::new();
    
    // Metadata
    body.extend_from_slice(metadata);
    
    // Transaction count
    body.extend_from_slice(&encode_compact_size(txs.len()));
    
    // Transactions
    for tx in txs {
        body.extend_from_slice(&tx.serialize());
    }
    
    body
}

/// Build the complete genesis block header (160 bytes)
/// From lotusd/src/primitives/block.h - CBlockHeader structure:
/// - hashPrevBlock (32 bytes)
/// - nBits (4 bytes)
/// - vTime (6 bytes)
/// - nReserved (2 bytes)
/// - nNonce (8 bytes)
/// - nHeaderVersion (1 byte)
/// - vSize (7 bytes)
/// - nHeight (4 bytes)
/// - hashEpochBlock (32 bytes)
/// - hashMerkleRoot (32 bytes)
/// - hashExtendedMetadata (32 bytes)
fn build_genesis_block_header(
    n_bits: u32,
    n_time: u64,
    n_nonce: u64,
    merkle_root: [u8; 32],
    extended_metadata_hash: [u8; 32],
    block_size: u64,
) -> [u8; 160] {
    let mut header = [0u8; 160];
    let mut offset = 0;
    
    // hashPrevBlock (32 bytes) - all zeros for genesis
    offset += 32;
    
    // nBits (4 bytes, little endian)
    header[offset..offset + 4].copy_from_slice(&n_bits.to_le_bytes());
    offset += 4;
    
    // vTime (6 bytes, little endian encoding of 48-bit timestamp)
    let time_bytes = [
        (n_time & 0xff) as u8,
        ((n_time >> 8) & 0xff) as u8,
        ((n_time >> 16) & 0xff) as u8,
        ((n_time >> 24) & 0xff) as u8,
        ((n_time >> 32) & 0xff) as u8,
        ((n_time >> 40) & 0xff) as u8,
    ];
    header[offset..offset + 6].copy_from_slice(&time_bytes);
    offset += 6;
    
    // nReserved (2 bytes) - all zeros
    offset += 2;
    
    // nNonce (8 bytes, little endian)
    header[offset..offset + 8].copy_from_slice(&n_nonce.to_le_bytes());
    offset += 8;
    
    // nHeaderVersion (1 byte) - always 1 for genesis
    header[offset] = 1;
    offset += 1;
    
    // vSize (7 bytes, little endian encoding of 56-bit size)
    let size_bytes = [
        (block_size & 0xff) as u8,
        ((block_size >> 8) & 0xff) as u8,
        ((block_size >> 16) & 0xff) as u8,
        ((block_size >> 24) & 0xff) as u8,
        ((block_size >> 32) & 0xff) as u8,
        ((block_size >> 40) & 0xff) as u8,
        ((block_size >> 48) & 0xff) as u8,
    ];
    header[offset..offset + 7].copy_from_slice(&size_bytes);
    offset += 7;
    
    // nHeight (4 bytes, little endian) - 0 for genesis
    offset += 4;
    
    // hashEpochBlock (32 bytes) - all zeros for genesis
    offset += 32;
    
    // hashMerkleRoot (32 bytes) - stored as internal bytes (little-endian)
    let mut merkle_root_bytes = merkle_root;
    merkle_root_bytes.reverse();  // Convert from hash output (big-endian) to internal format
    header[offset..offset + 32].copy_from_slice(&merkle_root_bytes);
    offset += 32;
    
    // hashExtendedMetadata (32 bytes) - stored as internal bytes (little-endian)
    let mut extended_metadata_bytes = extended_metadata_hash;
    extended_metadata_bytes.reverse();  // Convert from hash output (big-endian) to internal format
    header[offset..offset + 32].copy_from_slice(&extended_metadata_bytes);
    
    header
}

/// Create a genesis block for mining
/// Parameters match the testnet genesis block from chainparams.cpp:
/// - n_bits: difficulty target (0x1c100000 for testnet)
/// - n_time: block timestamp (will be updated during mining)
/// - target: target hash to beat
pub fn create_genesis_block(n_bits: u32, n_time: u64, target: [u8; 32]) -> GenesisBlock {
    // Create the coinbase transaction
    let genesis_tx = create_genesis_transaction();
    let txs = vec![genesis_tx];
    
    // Compute merkle root
    let merkle_root = compute_merkle_root(&txs);
    
    // Serialize and hash extended metadata (empty for genesis)
    let metadata = serialize_extended_metadata();
    let extended_metadata_hash = compute_serialize_hash(&metadata);
    
    // Serialize the block body
    let body = serialize_block_body(&metadata, &txs);
    
    // Calculate total block size (header 160 + body)
    let block_size = 160 + body.len() as u64;
    
    // Build the block header (with initial nonce = 0)
    let header = build_genesis_block_header(
        n_bits,
        n_time,
        0, // Initial nonce
        merkle_root,
        extended_metadata_hash,
        block_size,
    );
    
    GenesisBlock {
        header,
        body,
        target,
    }
}

/// Update the timestamp in a genesis block header
/// This allows periodic timestamp updates during mining
pub fn update_genesis_timestamp(header: &mut [u8; 160], n_time: u64) {
    let offset = 32 + 4; // After hashPrevBlock and nBits
    let time_bytes = [
        (n_time & 0xff) as u8,
        ((n_time >> 8) & 0xff) as u8,
        ((n_time >> 16) & 0xff) as u8,
        ((n_time >> 24) & 0xff) as u8,
        ((n_time >> 32) & 0xff) as u8,
        ((n_time >> 40) & 0xff) as u8,
    ];
    header[offset..offset + 6].copy_from_slice(&time_bytes);
}

/// Update the nonce in a genesis block header
pub fn update_genesis_nonce(header: &mut [u8; 160], n_nonce: u64) {
    let offset = 32 + 4 + 6 + 2; // After hashPrevBlock, nBits, vTime, nReserved
    header[offset..offset + 8].copy_from_slice(&n_nonce.to_le_bytes());
}

/// Get current Unix timestamp
pub fn get_current_timestamp() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("Time went backwards")
        .as_secs()
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_genesis_block_creation() {
        // Test with mainnet genesis parameters
        let n_bits = 0x1c100000;
        let n_time = 1624246260;
        let mut target = [0xffu8; 32];
        
        let genesis = create_genesis_block(n_bits, n_time, target);
        
        // Verify header size
        assert_eq!(genesis.header.len(), 160);
        
        // Verify body is not empty (contains transaction)
        assert!(genesis.body.len() > 0);
        
        // Verify nBits in header
        let n_bits_from_header = u32::from_le_bytes([
            genesis.header[32],
            genesis.header[33],
            genesis.header[34],
            genesis.header[35],
        ]);
        assert_eq!(n_bits_from_header, n_bits);
    }
    
    #[test]
    fn test_timestamp_update() {
        let n_bits = 0x1c100000;
        let n_time = 1624246260;
        let target = [0xffu8; 32];
        
        let mut genesis = create_genesis_block(n_bits, n_time, target);
        
        // Update timestamp
        let new_time = 1700000000;
        update_genesis_timestamp(&mut genesis.header, new_time);
        
        // Verify timestamp was updated
        let offset = 32 + 4;
        let time_from_header = u64::from_le_bytes([
            genesis.header[offset],
            genesis.header[offset + 1],
            genesis.header[offset + 2],
            genesis.header[offset + 3],
            genesis.header[offset + 4],
            genesis.header[offset + 5],
            0,
            0,
        ]);
        assert_eq!(time_from_header, new_time);
    }
}

