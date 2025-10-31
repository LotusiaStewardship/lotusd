# Testnet Version Filtering

## Overview

The testnet version filtering feature allows lotusd 10.x.x testnet nodes to perform an initial blockchain sync from 9.x.x mainnet nodes, then automatically disconnect from older version nodes after reaching a configured block height. This enables thorough testing of network upgrades in a realistic environment with real blockchain data.

## Use Case

When testing a major network upgrade (e.g., 9.x.x → 10.x.x):

1. **Initial Sync Phase**: Testnet nodes need blockchain history, which can be obtained from existing mainnet nodes
2. **Isolation Phase**: After sync, testnet nodes should only communicate with other 10.x.x testnet nodes to properly test the upgrade in isolation
3. **Upgrade Testing**: The isolated testnet network tests consensus changes, new features, and network behavior

## Configuration

### Command-Line Option

```bash
-testnetforkheight=<n>
```

**Parameters:**
- `<n>`: Block height at which to start rejecting connections from version 9.x.x and earlier nodes
- Default: `0` (feature disabled)

**Example Usage:**

```bash
# Disconnect from 9.x.x nodes after block height 100000
./lotusd -testnet -testnetforkheight=100000

# Disable version filtering (allow all versions)
./lotusd -testnet -testnetforkheight=0
```

### Configuration File

Add to `lotus.conf` under the `[test]` section:

```ini
[test]
testnetforkheight=100000
```

## Behavior

### Before Fork Height

- **All peer versions accepted**: Testnet nodes connect to both 9.x.x (mainnet) and 10.x.x (testnet) nodes
- **Purpose**: Allows initial blockchain synchronization from existing mainnet infrastructure
- **Logging**: Debug logging shows version parsing but no disconnections occur

### At Fork Height

When the blockchain reaches the configured `testnetforkheight`:

1. **Existing connections**: Peers running 9.x.x or earlier are immediately disconnected
2. **New connections**: Incoming VERSION messages from 9.x.x peers are rejected
3. **10.x.x peers**: All peers running version 10.0.0 or higher remain connected

### After Fork Height

- **Only 10.x.x peers**: Network operates exclusively with compatible upgrade nodes
- **Upgrade testing**: Isolated testnet can validate consensus changes, new opcodes, and protocol features
- **Automatic enforcement**: No manual intervention required

## Version Detection

### User Agent Parsing

The system parses the peer's user agent string to extract version information:

**Supported Formats:**
- `/lotusd:10.4.5(EB32.0)/` → Version 10.4.5
- `/lotusd:9.2.1/` → Version 9.2.1
- `lotusd:10.4.5` → Version 10.4.5

**Version Comparison:**
- Major, minor, and revision numbers are compared
- Versions < 10.0.0 are rejected after fork height
- Versions ≥ 10.0.0 are accepted

### Permissive Handling

If a peer's version cannot be parsed:
- **Connection allowed**: Unknown versions are not rejected
- **Rationale**: Avoids accidentally blocking legitimate nodes with non-standard user agents
- **Logging**: Parse failures are logged for debugging

## Logging

### Standard Logging

Connection events are logged at the default log level:

```
Disconnecting peer=5 (version 9.2.1, user agent: /lotusd:9.2.1(EB32.0)/) - 
incompatible client version for testnet at height 100150
```

### Debug Logging

Enable with `-debug=net` for detailed version filtering information:

```
Parsed client version from user agent '/lotusd:10.4.5(EB32.0)/': 10.4.5
Below fork height (95000 < 100000) - allowing peer version 9.2.1
Peer version 10.4.5 is acceptable (>= 10.0.0) at height 101000
```

## Mainnet Behavior

**Version filtering is DISABLED on mainnet:**
- The feature only activates when running with `-testnet`
- Mainnet nodes accept all protocol-compatible versions regardless of client version
- This prevents accidental network fragmentation on the production network

## Implementation Details

### Components

1. **versionfilter.h/cpp**: Core version parsing and comparison logic
2. **init.cpp**: Configuration option registration
3. **net_processing.cpp**: VERSION message handler integration
4. **CMakeLists.txt**: Build system integration

### Integration Point

Version checking occurs in `PeerManagerImpl::ProcessMessage()` during the VERSION message handler:

1. Peer sends VERSION message with user agent string
2. User agent is sanitized and stored as `cleanSubVer`
3. If testnet: parse version and check against fork height
4. If incompatible: disconnect immediately
5. If compatible: continue with normal connection flow

### Performance

- **Minimal overhead**: Version parsing uses compiled regex, executed once per connection
- **Early rejection**: Incompatible peers are disconnected before full handshake
- **No impact**: Zero performance impact on mainnet (feature disabled)

## Testing

### Manual Testing

1. **Set up two nodes:**
   ```bash
   # Node 1: Version 10.x.x testnet
   ./lotusd -testnet -testnetforkheight=100

   # Node 2: Version 9.x.x mainnet (or simulate with -uaclientversion)
   ./lotusd -connect=<node1_ip> -uaclientversion=9.2.1
   ```

2. **Observe behavior:**
   - Before height 100: Node 2 connects successfully
   - After height 100: Node 2 is disconnected
   - Check logs for disconnection messages

### Simulating Versions

Override the client version for testing:

```bash
# Simulate running version 9.2.1
./lotusd -testnet -uaclientversion=9.2.1

# Simulate running version 10.5.0  
./lotusd -testnet -uaclientversion=10.5.0
```

## Recommended Configuration

### For Christmas 2025 Upgrade Testing

Based on Second Samuel activation (December 21, 2025):

```bash
# Set fork height to Second Samuel activation or earlier
./lotusd -testnet -testnetforkheight=<second_samuel_height>
```

**Considerations:**
- Set fork height low enough to allow initial sync (e.g., current mainnet height + 1000 blocks)
- Ensure sufficient testnet peers running 10.x.x are available before reaching fork height
- Monitor peer counts: `lotus-cli getconnectioncount`

### Deployment Strategy

1. **Week 1**: Deploy 10.x.x nodes with `-testnetforkheight=0` (disabled)
2. **Week 2**: Configure fork height after initial sync completes
3. **Week 3+**: Test upgrade behavior in isolated testnet environment
4. **Pre-Christmas**: Verify consensus, features, and network stability

## Troubleshooting

### No Peers After Fork Height

**Symptom:** Peer count drops to zero after reaching fork height

**Solutions:**
1. Verify other 10.x.x testnet nodes are running
2. Check firewall rules allow testnet port (11605)
3. Add manual connections: `-addnode=<ip>`
4. Temporarily lower fork height if needed

### Version Parse Failures

**Symptom:** Logs show "Could not find version pattern"

**Cause:** Non-standard user agent format

**Solution:**
- Unknown versions are permitted (permissive handling)
- Use `-uaclientversion` to set standard format
- Check peer's actual version string

### Accidental Mainnet Filtering

**Symptom:** Mainnet nodes disconnecting unexpectedly

**Verification:**
```bash
# Check if accidentally running as testnet
lotus-cli getblockchaininfo | grep chain
# Should show "main", not "test"
```

**Solution:** Remove `-testnet` flag, restart node

## Security Considerations

- **No authentication**: Version strings are not cryptographically signed
- **Spoofing possible**: Malicious nodes can claim any version
- **Trust assumption**: Testnet environment assumes cooperative testing
- **Mainnet safety**: Feature disabled on mainnet to prevent abuse

## Future Enhancements

Potential improvements for future releases:

- Support for regex patterns in version filtering
- Whitelist/blacklist of specific versions
- Grace period before disconnection
- Configurable minimum version (not hardcoded to 10.0.0)
- Per-peer exemptions for development testing

## Related Documentation

- [Network Upgrades](upgrades/2025-winter-solstice.md)
- [Testnet Guide](testnet-guide.md) *(if exists)*
- [Configuration Options](lotus-conf.md)
- [Network Protocol](protocol-documentation.md) *(if exists)*

## Version History

- **v10.4.5**: Initial implementation of testnet version filtering
- Target upgrade: Second Samuel (December 21, 2025)

