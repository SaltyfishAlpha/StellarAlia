using System.Buffers.Binary;
using System.Collections.Generic;
using System.Text;

namespace StellarAlia.Bridge;

// ─────────────────────────────────────────────────────────────────────────────
// FieldBlobIO — managed mirror of native src/function/script/ScriptFieldBlob.
// Wire format documented there; both sides must stay byte-for-byte compatible.
//
// All multi-byte integers are little-endian. Strings are u16 length-prefixed
// UTF-8. UUID is two u64 LE (hi then lo).
// ─────────────────────────────────────────────────────────────────────────────

internal enum ScriptFieldKind : byte
{
    Bool        = 0,
    Int32       = 1,
    Float       = 2,
    String      = 3,
    Vec2        = 4,
    Vec3        = 5,
    Vec4        = 6,
    AssetRef    = 16,
    EntityRef   = 17,
    Color       = 18,
    Enum        = 19,
    Unsupported = 255,
}

internal sealed class BlobWriter
{
    private readonly List<byte> _buf = new();
    public int Count => _buf.Count;
    public byte[] ToArray() => _buf.ToArray();
    public void CopyTo(byte[] dst, int dstOffset = 0) => _buf.CopyTo(dst, dstOffset);

    public unsafe void WriteU8(byte v) => _buf.Add(v);

    public void WriteU16(ushort v) {
        Span<byte> b = stackalloc byte[2];
        BinaryPrimitives.WriteUInt16LittleEndian(b, v);
        _buf.AddRange(b.ToArray());
    }
    public void WriteU32(uint v) {
        Span<byte> b = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(b, v);
        _buf.AddRange(b.ToArray());
    }
    public void WriteI32(int v) {
        Span<byte> b = stackalloc byte[4];
        BinaryPrimitives.WriteInt32LittleEndian(b, v);
        _buf.AddRange(b.ToArray());
    }
    public void WriteF32(float v) {
        Span<byte> b = stackalloc byte[4];
        BinaryPrimitives.WriteSingleLittleEndian(b, v);
        _buf.AddRange(b.ToArray());
    }
    public void WriteU64(ulong v) {
        Span<byte> b = stackalloc byte[8];
        BinaryPrimitives.WriteUInt64LittleEndian(b, v);
        _buf.AddRange(b.ToArray());
    }

    public void WriteStr(string s) {
        byte[] utf8 = Encoding.UTF8.GetBytes(s ?? string.Empty);
        if (utf8.Length > 65535) {
            // Truncate at code-unit boundary; safe under UTF-8 because we slice
            // by byte count and the native side reads exactly the prefix length.
            var trunc = new byte[65535];
            System.Array.Copy(utf8, trunc, 65535);
            utf8 = trunc;
        }
        WriteU16((ushort)utf8.Length);
        _buf.AddRange(utf8);
    }
}

internal ref struct BlobReader
{
    private ReadOnlySpan<byte> _data;
    public bool Bad { get; private set; }
    public bool Eof => _data.Length == 0;
    public int  Remaining => _data.Length;

    public BlobReader(ReadOnlySpan<byte> data) {
        _data = data;
        Bad   = false;
    }

    private bool Take(int n) {
        if (Bad || _data.Length < n) { Bad = true; return false; }
        return true;
    }
    private void Advance(int n) { _data = _data.Slice(n); }

    public bool ReadU8(out byte v) {
        v = 0;
        if (!Take(1)) return false;
        v = _data[0]; Advance(1); return true;
    }
    public bool ReadU16(out ushort v) {
        v = 0;
        if (!Take(2)) return false;
        v = BinaryPrimitives.ReadUInt16LittleEndian(_data); Advance(2); return true;
    }
    public bool ReadU32(out uint v) {
        v = 0;
        if (!Take(4)) return false;
        v = BinaryPrimitives.ReadUInt32LittleEndian(_data); Advance(4); return true;
    }
    public bool ReadI32(out int v) {
        v = 0;
        if (!Take(4)) return false;
        v = BinaryPrimitives.ReadInt32LittleEndian(_data); Advance(4); return true;
    }
    public bool ReadF32(out float v) {
        v = 0;
        if (!Take(4)) return false;
        v = BinaryPrimitives.ReadSingleLittleEndian(_data); Advance(4); return true;
    }
    public bool ReadU64(out ulong v) {
        v = 0;
        if (!Take(8)) return false;
        v = BinaryPrimitives.ReadUInt64LittleEndian(_data); Advance(8); return true;
    }
    public bool ReadStr(out string s) {
        s = string.Empty;
        if (!ReadU16(out ushort len)) return false;
        if (!Take(len)) return false;
        s = Encoding.UTF8.GetString(_data.Slice(0, len));
        Advance(len);
        return true;
    }
    public bool Skip(int n) {
        if (!Take(n)) return false;
        Advance(n);
        return true;
    }
}
