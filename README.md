Simple UDP proxy server with traffic obfuscation capability using
a xor bitmask. 

Works only on Linux and fully supports both IPv4 and IPv6, used 
`recvmmsg()` и `sendmmsg()`

### Usage
```bash
flprox <source_port> <dest_hostname> <dest_port> <conn_timeout> <in_mask> <out_mask>
```
* `<source_port>` - listen port (both ipv4 and ipv6)
* `<dest_hostname>` - destination address or hostname
* `<dest_port>` - destination port
* `<conn_timeout>` - interval (in seconds) after which unused connections are cleared (each client is assigned a new port, similar to DNAT + SNAT behavior)
* `<in_mask>` - input mask for incoming packets (uint64, decimal)
* `<out_mask>` - output mask for outgoing packets

For obfuscation, use the same output mask on one server and input mask on the other. If you don’t want to use obfuscation at all, set both values to 0 0, though in that case, it might be better to just use `iptables` or `nftables` for this purpose.