// rawtun carries UDP datagrams inside a non-TCP/UDP IP protocol (UDP-Lite, GRE, SCTP, ...).
//
// Why: some ISPs' DPI boxes drop TCP, UDP and ICMP to a server's IP (an "IP block") but still
// pass other IP protocols both ways. A UDP-based VPN (hysteria2/tuic/wireguard) runs through
// this wrapper unchanged:
//
//	client app -> UDP 127.0.0.1:LISTEN -> [rawtun client] == ip proto N ==> [rawtun server] -> UDP FORWARD
//
// The client probes every protocol in -proto (default 136,47,132,253) and uses the first that
// the server answers, so a blocked protocol is skipped automatically. Both ends need raw
// sockets (admin on Windows, root on Linux). The server listens on all listed protocols at once.
package main

import (
	"encoding/binary"
	"flag"
	"log"
	"net"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

var (
	mode    = flag.String("mode", "", "client | server")
	proto   = flag.String("proto", "136,47,132,253", "IP protocol numbers to try, in order (client) / listen on (server)")
	server  = flag.String("server", "", "client: server IPv4 address")
	listen  = flag.String("listen", "127.0.0.1:44300", "client: local UDP address the VPN app connects to")
	bind    = flag.String("bind", "0.0.0.0", "local IPv4 address for the raw socket (client: the physical NIC)")
	forward = flag.String("forward", "127.0.0.1:443", "server: UDP address of the real VPN service")
	idle    = flag.Duration("idle", 3*time.Minute, "server: drop a client after this much silence")
)

// Wire header, shaped as a UDP-Lite header so NAT boxes that track ports keep working:
// source port, destination port, checksum coverage (8 = header only), checksum (unused).
// The source port carries the client's session id, so several flows/clients stay apart.
// Two id values are reserved for the liveness probe (never used as a real session id).
const (
	hdrLen    = 8
	probeID   = 0xFFFE // client -> server: "are you there?"
	probeAck  = 0xFFFF // server -> client: "yes"
	probePort = 7      // dport of a probe packet
)

func putHdr(b []byte, sport, dport uint16) {
	binary.BigEndian.PutUint16(b[0:], sport)
	binary.BigEndian.PutUint16(b[2:], dport)
	binary.BigEndian.PutUint16(b[4:], hdrLen)
	binary.BigEndian.PutUint16(b[6:], 0xFFFF)
}

func parseProtos(s string) []int {
	var out []int
	for _, p := range strings.Split(s, ",") {
		if n, err := strconv.Atoi(strings.TrimSpace(p)); err == nil && n > 0 && n < 256 {
			out = append(out, n)
		}
	}
	return out
}

func main() {
	flag.Parse()
	protos := parseProtos(*proto)
	if len(protos) == 0 {
		log.Fatal("-proto: no valid protocol numbers")
	}
	switch *mode {
	case "client":
		runClient(protos)
	case "server":
		runServer(protos)
	default:
		log.Fatal("-mode must be client or server")
	}
}

// ── Client ──────────────────────────────────────────────────────────────

func runClient(protos []int) {
	srv := net.ParseIP(*server)
	if srv == nil {
		log.Fatal("client: -server must be an IPv4 address")
	}
	proto := pickProto(srv, protos)
	network := "ip4:" + strconv.Itoa(proto)
	raw, err := net.ListenIP(network, &net.IPAddr{IP: net.ParseIP(*bind)})
	if err != nil {
		log.Fatalf("raw socket (%s, needs admin): %v", network, err)
	}
	laddr, err := net.ResolveUDPAddr("udp4", *listen)
	if err != nil {
		log.Fatal(err)
	}
	udp, err := net.ListenUDP("udp4", laddr)
	if err != nil {
		log.Fatal(err)
	}
	_ = raw.SetReadBuffer(4 << 20)
	_ = raw.SetWriteBuffer(4 << 20)
	_ = udp.SetReadBuffer(4 << 20)
	_ = udp.SetWriteBuffer(4 << 20)
	log.Printf("client: %s <-> %s over %s (proto %d)", *listen, srv, network, proto)

	// Each local UDP peer (one per VPN connection) gets its own session id = wire source port.
	var mu sync.Mutex
	byAddr := map[string]uint16{}
	byID := map[uint16]*net.UDPAddr{}
	next := uint16(40000)
	dst := &net.IPAddr{IP: srv}

	// Keep the chosen path warm and detect if it dies (NAT timeout): a background prober.
	go func() {
		p := make([]byte, hdrLen)
		putHdr(p, probeID, probePort)
		for range time.Tick(20 * time.Second) {
			_, _ = raw.WriteToIP(p, dst)
		}
	}()

	go func() { // server -> app
		buf := make([]byte, 65535)
		for {
			n, from, err := raw.ReadFromIP(buf)
			if err != nil {
				log.Fatalf("raw read: %v", err)
			}
			if !from.IP.Equal(srv) || n < hdrLen {
				continue
			}
			id := binary.BigEndian.Uint16(buf[2:]) // server echoes our id as destination port
			if id == probeAck {
				continue
			}
			mu.Lock()
			peer := byID[id]
			mu.Unlock()
			if peer != nil {
				_, _ = udp.WriteToUDP(buf[hdrLen:n], peer)
			}
		}
	}()

	buf := make([]byte, 65535)
	for { // app -> server
		n, peer, err := udp.ReadFromUDP(buf[hdrLen:])
		if err != nil {
			log.Fatalf("udp read: %v", err)
		}
		key := peer.String()
		mu.Lock()
		id, ok := byAddr[key]
		if !ok {
			id = next
			next++
			if next >= probeID {
				next = 40000
			}
			if old := byID[id]; old != nil {
				delete(byAddr, old.String())
			}
			byAddr[key] = id
			byID[id] = peer
		}
		mu.Unlock()
		putHdr(buf, id, 443)
		if _, err := raw.WriteToIP(buf[:hdrLen+n], dst); err != nil {
			log.Printf("raw write: %v", err)
		}
	}
}

// pickProto returns the first protocol the server answers a probe on. If none answer (the
// server may be old / single-protocol, or the probe reply was lost), it falls back to the
// first protocol in the list so a working server is still tried.
func pickProto(srv net.IP, protos []int) int {
	type res struct {
		proto int
		ok    bool
	}
	dst := &net.IPAddr{IP: srv}
	probe := make([]byte, hdrLen)
	putHdr(probe, probeID, probePort)

	for _, p := range protos {
		raw, err := net.ListenIP("ip4:"+strconv.Itoa(p), &net.IPAddr{IP: net.ParseIP(*bind)})
		if err != nil {
			log.Printf("probe proto %d: %v", p, err)
			continue
		}
		ch := make(chan bool, 1)
		go func() {
			buf := make([]byte, 1500)
			_ = raw.SetReadDeadline(time.Now().Add(1500 * time.Millisecond))
			for {
				n, from, err := raw.ReadFromIP(buf)
				if err != nil {
					ch <- false
					return
				}
				if from.IP.Equal(srv) && n >= hdrLen && binary.BigEndian.Uint16(buf[2:]) == probeAck {
					ch <- true
					return
				}
			}
		}()
		for i := 0; i < 3; i++ {
			_, _ = raw.WriteToIP(probe, dst)
			time.Sleep(120 * time.Millisecond)
		}
		ok := <-ch
		raw.Close()
		if ok {
			log.Printf("probe: proto %d works", p)
			return p
		}
		log.Printf("probe: proto %d no answer", p)
	}
	log.Printf("probe: none answered, falling back to proto %d", protos[0])
	return protos[0]
}

// ── Server ──────────────────────────────────────────────────────────────

type session struct {
	conn *net.UDPConn
	seen time.Time
	// Destination address used in the UDP-Lite pseudo-header of replies (see csumDst()).
	csum atomic.Pointer[net.IP]
}

// ── UDP-Lite checksums, for clients that use a real kernel UDP-Lite socket ──
//
// A raw-socket client (Windows rawtun) ignores the checksum field. An Android app cannot open
// raw sockets, but it can open an ordinary UDP-Lite datagram socket (IP protocol 136), and
// then the phone's kernel verifies the checksum of everything we send back. The checksum
// covers a pseudo-header with both IP addresses, and NAT boxes come in two kinds:
//   - they understand UDP-Lite and fix the checksum when rewriting the address ("standard":
//     we compute it against the client's public address), or
//   - they rewrite only the IP header ("compensated": we compute it against the client's
//     private address, which the client reports, so it is valid after the rewrite).
// The client asks for both variants ("RTUNH"), sees which one its kernel accepts and tells
// us ("RTUNM"). Control packets travel to destination port probePort.

var localIPs sync.Map // client IP string -> our source IP toward it

func localIPFor(client net.IP) net.IP {
	if v, ok := localIPs.Load(client.String()); ok {
		return v.(net.IP)
	}
	ip := net.IPv4zero
	if c, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: client, Port: 9}); err == nil {
		ip = c.LocalAddr().(*net.UDPAddr).IP.To4()
		c.Close()
	}
	localIPs.Store(client.String(), ip)
	return ip
}

// seal writes the 8-byte header with a valid UDP-Lite checksum (coverage: header only).
// pkt is the whole datagram (header + payload); src/dst are the pseudo-header addresses.
func seal(pkt []byte, sport, dport uint16, src, dst net.IP) {
	binary.BigEndian.PutUint16(pkt[0:], sport)
	binary.BigEndian.PutUint16(pkt[2:], dport)
	binary.BigEndian.PutUint16(pkt[4:], hdrLen)
	pkt[6], pkt[7] = 0, 0
	var sum uint32
	add := func(b []byte) {
		for i := 0; i+1 < len(b); i += 2 {
			sum += uint32(b[i])<<8 | uint32(b[i+1])
		}
	}
	add(src.To4())
	add(dst.To4())
	sum += 136
	sum += uint32(len(pkt))
	add(pkt[:hdrLen])
	for sum>>16 != 0 {
		sum = sum&0xFFFF + sum>>16
	}
	c := ^uint16(sum)
	if c == 0 {
		c = 0xFFFF
	}
	binary.BigEndian.PutUint16(pkt[6:], c)
}

func runServer(protos []int) {
	fwd, err := net.ResolveUDPAddr("udp4", *forward)
	if err != nil {
		log.Fatal(err)
	}
	log.Printf("server: proto %v -> %s", protos, fwd)

	var mu sync.Mutex
	sessions := map[string]*session{} // key: client IP + session id

	go func() { // expire idle sessions
		for range time.Tick(30 * time.Second) {
			mu.Lock()
			for k, s := range sessions {
				if time.Since(s.seen) > *idle {
					s.conn.Close()
					delete(sessions, k)
				}
			}
			mu.Unlock()
		}
	}()

	var wg sync.WaitGroup
	for _, p := range protos {
		network := "ip4:" + strconv.Itoa(p)
		raw, err := net.ListenIP(network, &net.IPAddr{IP: net.ParseIP(*bind)})
		if err != nil {
			log.Printf("listen %s (needs root): %v", network, err)
			continue
		}
		_ = raw.SetReadBuffer(4 << 20)
		_ = raw.SetWriteBuffer(4 << 20)
		wg.Add(1)
		go func(raw *net.IPConn, network string) {
			defer wg.Done()
			serveProto(raw, network, fwd, &mu, sessions)
		}(raw, network)
	}
	wg.Wait()
	log.Fatal("server: no raw socket could be opened")
}

func serveProto(raw *net.IPConn, network string, fwd *net.UDPAddr,
	mu *sync.Mutex, sessions map[string]*session) {
	// Checksum mode chosen by a client before its session exists: key -> private IP.
	modes := map[string]net.IP{}
	buf := make([]byte, 65535)
	for {
		n, from, err := raw.ReadFromIP(buf)
		if err != nil {
			log.Printf("%s read: %v", network, err)
			return
		}
		if n < hdrLen {
			continue
		}
		client := append(net.IP(nil), from.IP.To4()...)
		id := binary.BigEndian.Uint16(buf[0:])
		dport := binary.BigEndian.Uint16(buf[2:])
		key := client.String() + "/" + strconv.Itoa(int(id))
		src := localIPFor(client)
		dst := &net.IPAddr{IP: client}
		body := buf[hdrLen:n]

		// Control packets from kernel-socket clients.
		if dport == probePort && len(body) >= 5 && string(body[:4]) == "RTUN" {
			switch body[4] {
			case 'H': // hello + private IPv4: answer with both checksum variants
				if len(body) < 9 {
					continue
				}
				priv := append(net.IP(nil), body[5:9]...)
				a := append(make([]byte, hdrLen), "RTUNA"...)
				seal(a, probePort, id, src, client)
				_, _ = raw.WriteToIP(a, dst)
				b := append(make([]byte, hdrLen), "RTUNB"...)
				seal(b, probePort, id, src, priv)
				_, _ = raw.WriteToIP(b, dst)
			case 'M': // mode: 0 standard, 1 compensated (+ private IPv4)
				if len(body) < 10 {
					continue
				}
				cs := client
				if body[5] == 1 {
					cs = append(net.IP(nil), body[6:10]...)
				}
				mu.Lock()
				modes[key] = cs
				if s := sessions[key]; s != nil {
					s.csum.Store(&cs)
				}
				mu.Unlock()
				k := append(make([]byte, hdrLen), "RTUNK"...)
				seal(k, probePort, id, src, cs)
				_, _ = raw.WriteToIP(k, dst)
				log.Printf("client %s on %s: checksum against %s", key, network, cs)
			}
			continue
		}
		// Liveness probe of raw-socket clients: answer on the same protocol.
		if id == probeID {
			ack := make([]byte, hdrLen)
			seal(ack, probePort, probeAck, src, client)
			_, _ = raw.WriteToIP(ack, dst)
			continue
		}
		mu.Lock()
		s := sessions[key]
		if s == nil {
			conn, err := net.DialUDP("udp4", nil, fwd)
			if err != nil {
				mu.Unlock()
				log.Printf("dial %s: %v", fwd, err)
				continue
			}
			_ = conn.SetReadBuffer(4 << 20)
			_ = conn.SetWriteBuffer(4 << 20)
			s = &session{conn: conn}
			cs := client
			if m := modes[key]; m != nil {
				cs = m
			}
			s.csum.Store(&cs)
			sessions[key] = s
			log.Printf("new session %s on %s", key, network)
			go func(s *session, client net.IP, id uint16) { // VPN service -> client
				out := make([]byte, 65535)
				dst := &net.IPAddr{IP: client}
				src := localIPFor(client)
				for {
					m, err := s.conn.Read(out[hdrLen:])
					if err != nil {
						return
					}
					seal(out[:hdrLen+m], 443, id, src, *s.csum.Load())
					_, _ = raw.WriteToIP(out[:hdrLen+m], dst)
				}
			}(s, client, id)
		}
		s.seen = time.Now()
		mu.Unlock()
		_, _ = s.conn.Write(body)
	}
}
