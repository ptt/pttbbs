package daemon

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

const (
	IPCMagic   uint32 = 0x49504356 // 'IPCV'
	IPCVersion uint16 = 1

	IPCUserIDLen = 16
	IPCTopicLen  = 64
	IPCTextLen   = 128

	// AiduRawMask masks off the 19-bit .DIR index hint [63:45] in aidu_t
	AiduRawMask uint64 = 0x00001FFFFFFFFFFF
)

const (
	// 0x0001 - 0x000F: Lifecycle & Control
	IPCTypeRegister   uint16 = 0x0001
	IPCTypeUnregister uint16 = 0x0002
	IPCTypeAck        uint16 = 0x0003
	IPCTypePing       uint16 = 0x0004
	IPCTypePong       uint16 = 0x0005
	IPCTypeStatsReq   uint16 = 0x0006
	IPCTypeStatsResp  uint16 = 0x0007

	// 0x0010 - 0x001F: Waterball (wmsg)
	IPCTypeWmsgSend    uint16 = 0x0010
	IPCTypeWmsgDeliver uint16 = 0x0011

	// 0x0020 - 0x002F: PubSub (Live Post & Channels)
	IPCTypeSub         uint16 = 0x0020
	IPCTypeUnsub       uint16 = 0x0021
	IPCTypePub         uint16 = 0x0022
	IPCTypeLiveComment uint16 = 0x0023

	// 0x0030 - 0x003F: Two-Way Sessions, Talk & Chess
	IPCTypeTalkReq      uint16 = 0x0030
	IPCTypeTalkCancel   uint16 = 0x0031
	IPCTypeSessionReq   uint16 = 0x0030
	IPCTypeSessionResp  uint16 = 0x0031
	IPCTypeSessionData  uint16 = 0x0032
	IPCTypeSessionClose uint16 = 0x0033
	IPCTypeChessReq     uint16 = 0x0034
	IPCTypeChessResp    uint16 = 0x0035
	IPCTypeChessMove    uint16 = 0x0036

	// 0x0040 - 0x004F: XChat / Chat Room
	IPCTypeChatMsg  uint16 = 0x0040
	IPCTypeChatRoom uint16 = 0x0041
	IPCTypeChatSys  uint16 = 0x0042

	// 0x0050 - 0x005F: Broadcast
	IPCTypeSysBroadcast uint16 = 0x0050
)

// Header represents the 16-byte fixed C struct header:
// uint32 magic, uint16 version, uint16 type, uint32 seq, uint32 len
type Header struct {
	Magic   uint32
	Version uint16
	Type    uint16
	Seq     uint32
	Len     uint32
}

func (h *Header) Encode() []byte {
	buf := make([]byte, 16)
	binary.LittleEndian.PutUint32(buf[0:4], h.Magic)
	binary.LittleEndian.PutUint16(buf[4:6], h.Version)
	binary.LittleEndian.PutUint16(buf[6:8], h.Type)
	binary.LittleEndian.PutUint32(buf[8:12], h.Seq)
	binary.LittleEndian.PutUint32(buf[12:16], h.Len)
	return buf
}

func ReadHeader(r io.Reader) (*Header, error) {
	buf := make([]byte, 16)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	h := &Header{
		Magic:   binary.LittleEndian.Uint32(buf[0:4]),
		Version: binary.LittleEndian.Uint16(buf[4:6]),
		Type:    binary.LittleEndian.Uint16(buf[6:8]),
		Seq:     binary.LittleEndian.Uint32(buf[8:12]),
		Len:     binary.LittleEndian.Uint32(buf[12:16]),
	}
	if h.Magic != IPCMagic {
		return nil, fmt.Errorf("invalid IPC magic: 0x%08x (expected 0x%08x)", h.Magic, IPCMagic)
	}
	if h.Version != IPCVersion {
		return nil, fmt.Errorf("unsupported IPC version: %d (expected %d)", h.Version, IPCVersion)
	}
	return h, nil
}

// Fixed-size string helper
func cstrToString(b []byte) string {
	idx := bytes.IndexByte(b, 0)
	if idx >= 0 {
		return string(b[:idx])
	}
	return string(b)
}

func stringToCstr(s string, out []byte) {
	for i := range out {
		out[i] = 0
	}
	copy(out, s)
	if len(s) < len(out) {
		out[len(s)] = 0
	} else {
		out[len(out)-1] = 0
	}
}

// Payloads

type RegisterPayload struct {
	UserID [IPCUserIDLen]byte
	PID    uint32
	USlot  int32
}

func DecodeRegisterPayload(data []byte) (*RegisterPayload, error) {
	if len(data) < 24 {
		return nil, fmt.Errorf("payload too short for register: %d bytes", len(data))
	}
	p := &RegisterPayload{}
	copy(p.UserID[:], data[:IPCUserIDLen])
	p.PID = binary.LittleEndian.Uint32(data[16:20])
	p.USlot = int32(binary.LittleEndian.Uint32(data[20:24]))
	return p, nil
}

func (p *RegisterPayload) Encode() []byte {
	buf := make([]byte, 24)
	copy(buf[:IPCUserIDLen], p.UserID[:])
	binary.LittleEndian.PutUint32(buf[16:20], p.PID)
	binary.LittleEndian.PutUint32(buf[20:24], uint32(p.USlot))
	return buf
}

func (p *RegisterPayload) UserIDString() string {
	return cstrToString(p.UserID[:])
}

type AckPayload struct {
	Code    int32
	Message [64]byte
}

func (p *AckPayload) Encode() []byte {
	buf := make([]byte, 68)
	binary.LittleEndian.PutUint32(buf[0:4], uint32(p.Code))
	copy(buf[4:], p.Message[:])
	return buf
}

func DecodeAckPayload(data []byte) (*AckPayload, error) {
	if len(data) < 4 {
		return nil, fmt.Errorf("payload too short for ack: %d bytes", len(data))
	}
	p := &AckPayload{
		Code: int32(binary.LittleEndian.Uint32(data[0:4])),
	}
	if len(data) >= 68 {
		copy(p.Message[:], data[4:68])
	}
	return p, nil
}

func (p *AckPayload) MessageString() string {
	return cstrToString(p.Message[:])
}

type WmsgPayload struct {
	FromUserID [IPCUserIDLen]byte
	ToUserID   [IPCUserIDLen]byte
	FromPID    uint32
	FromUSlot  int32
	MsgMode    int32
	Timestamp  uint32
	Text       [IPCTextLen]byte
}

const WmsgPayloadSize = 16 + 16 + 4 + 4 + 4 + 4 + 128 // 176 bytes

func DecodeWmsgPayload(data []byte) (*WmsgPayload, error) {
	if len(data) < WmsgPayloadSize {
		return nil, fmt.Errorf("payload too short for wmsg: %d bytes (need %d)", len(data), WmsgPayloadSize)
	}
	p := &WmsgPayload{}
	copy(p.FromUserID[:], data[0:16])
	copy(p.ToUserID[:], data[16:32])
	p.FromPID = binary.LittleEndian.Uint32(data[32:36])
	p.FromUSlot = int32(binary.LittleEndian.Uint32(data[36:40]))
	p.MsgMode = int32(binary.LittleEndian.Uint32(data[40:44]))
	p.Timestamp = binary.LittleEndian.Uint32(data[44:48])
	copy(p.Text[:], data[48:48+IPCTextLen])
	return p, nil
}

func (p *WmsgPayload) Encode() []byte {
	buf := make([]byte, WmsgPayloadSize)
	copy(buf[0:16], p.FromUserID[:])
	copy(buf[16:32], p.ToUserID[:])
	binary.LittleEndian.PutUint32(buf[32:36], p.FromPID)
	binary.LittleEndian.PutUint32(buf[36:40], uint32(p.FromUSlot))
	binary.LittleEndian.PutUint32(buf[40:44], uint32(p.MsgMode))
	binary.LittleEndian.PutUint32(buf[44:48], p.Timestamp)
	copy(buf[48:48+IPCTextLen], p.Text[:])
	return buf
}

func (p *WmsgPayload) FromUserIDString() string {
	return cstrToString(p.FromUserID[:])
}

func (p *WmsgPayload) ToUserIDString() string {
	return cstrToString(p.ToUserID[:])
}

func (p *WmsgPayload) TextString() string {
	return cstrToString(p.Text[:])
}

type TopicKey struct {
	Bid uint32
	Aid uint64
}

func (k TopicKey) String() string {
	return fmt.Sprintf("bid=%d:aid=0x%016x", k.Bid, k.Aid)
}

type PubSubPayload struct {
	Bid       uint32
	Timestamp uint32
	Aid       uint64
	Text      [IPCTextLen]byte
	// FSize is the article size right after the comment was appended (0:
	// unknown), passed through to subscribers so they can skip comments they
	// already read from the file.
	FSize uint64
}

// PubSubPayloadMinSize is the size without FSize, still accepted from older
// clients; PubSubPayloadSize is what we encode.
const (
	PubSubPayloadMinSize = 4 + 4 + 8 + IPCTextLen   // 144 bytes
	PubSubPayloadSize    = PubSubPayloadMinSize + 8 // 152 bytes
)

func DecodePubSubPayload(data []byte) (*PubSubPayload, error) {
	if len(data) < PubSubPayloadMinSize {
		return nil, fmt.Errorf("payload too short for pubsub: %d bytes (need %d)", len(data), PubSubPayloadMinSize)
	}
	p := &PubSubPayload{}
	p.Bid = binary.LittleEndian.Uint32(data[0:4])
	p.Timestamp = binary.LittleEndian.Uint32(data[4:8])
	p.Aid = binary.LittleEndian.Uint64(data[8:16]) & AiduRawMask
	copy(p.Text[:], data[16:16+IPCTextLen])
	if len(data) >= PubSubPayloadSize {
		p.FSize = binary.LittleEndian.Uint64(data[PubSubPayloadMinSize:PubSubPayloadSize])
	}
	return p, nil
}

func (p *PubSubPayload) Encode() []byte {
	buf := make([]byte, PubSubPayloadSize)
	binary.LittleEndian.PutUint32(buf[0:4], p.Bid)
	binary.LittleEndian.PutUint32(buf[4:8], p.Timestamp)
	binary.LittleEndian.PutUint64(buf[8:16], p.Aid & AiduRawMask)
	copy(buf[16:16+IPCTextLen], p.Text[:])
	binary.LittleEndian.PutUint64(buf[PubSubPayloadMinSize:PubSubPayloadSize], p.FSize)
	return buf
}

func (p *PubSubPayload) Key() TopicKey {
	return TopicKey{Bid: p.Bid, Aid: p.Aid & AiduRawMask}
}

func (p *PubSubPayload) TextString() string {
	return cstrToString(p.Text[:])
}

// TalkPayload (IPC_TYPE_TALK_REQ)
type TalkPayload struct {
	ToUserID   [IPCUserIDLen]byte
	FromUserID [IPCUserIDLen]byte
	FromPID    uint32
	FromUSlot  int32
	Sig        int32
	IsUrgent   int32
	Pad        [24]byte
}

const TalkPayloadSize = 16 + 16 + 4 + 4 + 4 + 4 + 24 // 72 bytes

func DecodeTalkPayload(data []byte) (*TalkPayload, error) {
	if len(data) < TalkPayloadSize {
		return nil, fmt.Errorf("payload too short for talk: %d bytes (need %d)", len(data), TalkPayloadSize)
	}
	p := &TalkPayload{}
	copy(p.ToUserID[:], data[0:16])
	copy(p.FromUserID[:], data[16:32])
	p.FromPID = binary.LittleEndian.Uint32(data[32:36])
	p.FromUSlot = int32(binary.LittleEndian.Uint32(data[36:40]))
	p.Sig = int32(binary.LittleEndian.Uint32(data[40:44]))
	p.IsUrgent = int32(binary.LittleEndian.Uint32(data[44:48]))
	copy(p.Pad[:], data[48:72])
	return p, nil
}

func (p *TalkPayload) Encode() []byte {
	buf := make([]byte, TalkPayloadSize)
	copy(buf[0:16], p.ToUserID[:])
	copy(buf[16:32], p.FromUserID[:])
	binary.LittleEndian.PutUint32(buf[32:36], p.FromPID)
	binary.LittleEndian.PutUint32(buf[36:40], uint32(p.FromUSlot))
	binary.LittleEndian.PutUint32(buf[40:44], uint32(p.Sig))
	binary.LittleEndian.PutUint32(buf[44:48], uint32(p.IsUrgent))
	copy(buf[48:72], p.Pad[:])
	return buf
}

func (p *TalkPayload) ToUserIDString() string {
	return cstrToString(p.ToUserID[:])
}

func (p *TalkPayload) FromUserIDString() string {
	return cstrToString(p.FromUserID[:])
}

// ChatPayload (IPC_TYPE_CHAT_MSG)
type ChatPayload struct {
	Room      [32]byte
	UserID    [IPCUserIDLen]byte
	ChatID    [IPCUserIDLen]byte
	Timestamp uint32
	Text      [IPCTextLen]byte
}

const ChatPayloadSize = 32 + 16 + 16 + 4 + 128 // 196 bytes

func DecodeChatPayload(data []byte) (*ChatPayload, error) {
	if len(data) < ChatPayloadSize {
		return nil, fmt.Errorf("payload too short for chat: %d bytes (need %d)", len(data), ChatPayloadSize)
	}
	p := &ChatPayload{}
	copy(p.Room[:], data[0:32])
	copy(p.UserID[:], data[32:48])
	copy(p.ChatID[:], data[48:64])
	p.Timestamp = binary.LittleEndian.Uint32(data[64:68])
	copy(p.Text[:], data[68:68+IPCTextLen])
	return p, nil
}

func (p *ChatPayload) Encode() []byte {
	buf := make([]byte, ChatPayloadSize)
	copy(buf[0:32], p.Room[:])
	copy(buf[32:48], p.UserID[:])
	copy(buf[48:64], p.ChatID[:])
	binary.LittleEndian.PutUint32(buf[64:68], p.Timestamp)
	copy(buf[68:68+IPCTextLen], p.Text[:])
	return buf
}

func (p *ChatPayload) RoomString() string {
	return cstrToString(p.Room[:])
}

func (p *ChatPayload) UserIDString() string {
	return cstrToString(p.UserID[:])
}

func (p *ChatPayload) ChatIDString() string {
	return cstrToString(p.ChatID[:])
}

func (p *ChatPayload) TextString() string {
	return cstrToString(p.Text[:])
}

func MakeAckPacket(seq uint32, code int32, msg string) []byte {
	ack := &AckPayload{Code: code}
	stringToCstr(msg, ack.Message[:])
	payloadBytes := ack.Encode()

	hdr := &Header{
		Magic:   IPCMagic,
		Version: IPCVersion,
		Type:    IPCTypeAck,
		Seq:     seq,
		Len:     uint32(len(payloadBytes)),
	}
	return append(hdr.Encode(), payloadBytes...)
}

type StatsPayload struct {
	ActiveClients   uint32
	RegisteredUsers uint32
	ActiveSessions  uint32
	ActiveTopics    uint32
	ActiveChess     uint32
	ActiveXChat     uint32
	TotalWmsg       uint64
	TotalTalk       uint64
	TotalLive       uint64
	UptimeSec       uint32
	Pad             [24]byte
}

func (s *StatsPayload) Encode() []byte {
	buf := make([]byte, 76)
	binary.LittleEndian.PutUint32(buf[0:4], s.ActiveClients)
	binary.LittleEndian.PutUint32(buf[4:8], s.RegisteredUsers)
	binary.LittleEndian.PutUint32(buf[8:12], s.ActiveSessions)
	binary.LittleEndian.PutUint32(buf[12:16], s.ActiveTopics)
	binary.LittleEndian.PutUint32(buf[16:20], s.ActiveChess)
	binary.LittleEndian.PutUint32(buf[20:24], s.ActiveXChat)
	binary.LittleEndian.PutUint64(buf[24:32], s.TotalWmsg)
	binary.LittleEndian.PutUint64(buf[32:40], s.TotalTalk)
	binary.LittleEndian.PutUint64(buf[40:48], s.TotalLive)
	binary.LittleEndian.PutUint32(buf[48:52], s.UptimeSec)
	copy(buf[52:76], s.Pad[:])
	return buf
}

func DecodeStatsPayload(data []byte) (*StatsPayload, error) {
	if len(data) < 52 {
		return nil, fmt.Errorf("stats payload too short: %d", len(data))
	}
	s := &StatsPayload{
		ActiveClients:   binary.LittleEndian.Uint32(data[0:4]),
		RegisteredUsers: binary.LittleEndian.Uint32(data[4:8]),
		ActiveSessions:  binary.LittleEndian.Uint32(data[8:12]),
		ActiveTopics:    binary.LittleEndian.Uint32(data[12:16]),
		ActiveChess:     binary.LittleEndian.Uint32(data[16:20]),
		ActiveXChat:     binary.LittleEndian.Uint32(data[20:24]),
		TotalWmsg:       binary.LittleEndian.Uint64(data[24:32]),
		TotalTalk:       binary.LittleEndian.Uint64(data[32:40]),
		TotalLive:       binary.LittleEndian.Uint64(data[40:48]),
		UptimeSec:       binary.LittleEndian.Uint32(data[48:52]),
	}
	if len(data) >= 76 {
		copy(s.Pad[:], data[52:76])
	}
	return s, nil
}
