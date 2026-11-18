// SPDX-License-Identifier: GPL-2.0-only
// fwfetch helpers: everything here only READS. Nothing is ever written to a disk or partition.
//   - AlignedStream : sector-aligned read-only wrapper (needed for \\.\PhysicalDriveN)
//   - PartTable     : MBR / GPT parser
//   - Ext4          : minimal read-only ext4 reader (extents, symlinks, linear directory scan)
//   - Initramfs     : uImage / gzip / plain cpio(newc) reader
//   - Ihex          : Intel HEX -> binary
// Kept to C# 5 so Windows PowerShell 5.1 (csc of .NET Framework 4.x) can compile it with Add-Type.
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace W4T.FwFetch
{
    public sealed class AlignedStream : Stream
    {
        private readonly Stream inner;
        private readonly int blk;
        private long pos;
        private long cacheStart = -1;
        private byte[] cache;
        private int cacheLen;
        public AlignedStream(Stream inner, int blockSize)
        {
            this.inner = inner; this.blk = blockSize; this.cache = new byte[blockSize * 16];
        }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return true; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { try { return inner.Length; } catch (Exception) { return long.MaxValue; } } }
        public override long Position { get { return pos; } set { pos = value; } }
        public override void Flush() { }
        public override long Seek(long offset, SeekOrigin origin)
        {
            if (origin == SeekOrigin.Begin) pos = offset;
            else if (origin == SeekOrigin.Current) pos += offset;
            else pos = Length + offset;
            return pos;
        }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
        public override int Read(byte[] buffer, int offset, int count)
        {
            int done = 0;
            while (done < count)
            {
                long cs = pos - (pos % (long)cache.Length);
                if (cs != cacheStart)
                {
                    inner.Seek(cs, SeekOrigin.Begin);
                    int got = 0;
                    while (got < cache.Length)
                    {
                        int n = inner.Read(cache, got, cache.Length - got);
                        if (n <= 0) break;
                        got += n;
                    }
                    cacheStart = cs; cacheLen = got;
                }
                int off = (int)(pos - cacheStart);
                if (off >= cacheLen) break;
                int take = Math.Min(count - done, cacheLen - off);
                Buffer.BlockCopy(cache, off, buffer, offset + done, take);
                done += take; pos += take;
            }
            return done;
        }
    }

    public sealed class PartInfo
    {
        public int Index; public long Offset; public long Length; public string Kind;
    }

    public static class PartTable
    {
        public static List<PartInfo> Read(Stream s)
        {
            var list = new List<PartInfo>();
            byte[] b = new byte[512];
            s.Seek(0, SeekOrigin.Begin);
            if (Fill(s, b, 512) != 512 || b[510] != 0x55 || b[511] != 0xAA) return list;
            bool gpt = false;
            for (int i = 0; i < 4; i++)
            {
                int o = 446 + i * 16; byte type = b[o + 4];
                uint lba = BitConverter.ToUInt32(b, o + 8), cnt = BitConverter.ToUInt32(b, o + 12);
                if (type == 0xEE) gpt = true;
                else if (type != 0 && type != 0x05 && type != 0x0F && type != 0x85 && cnt > 0)
                    list.Add(new PartInfo { Index = i + 1, Offset = (long)lba * 512, Length = (long)cnt * 512, Kind = "mbr-0x" + type.ToString("x2") });
            }
            if (!gpt) return list;
            list.Clear();
            s.Seek(512, SeekOrigin.Begin);
            byte[] h = new byte[512];
            if (Fill(s, h, 512) != 512 || Encoding.ASCII.GetString(h, 0, 8) != "EFI PART") return list;
            long entLba = BitConverter.ToInt64(h, 72); uint num = BitConverter.ToUInt32(h, 80), esz = BitConverter.ToUInt32(h, 84);
            if (esz < 128 || esz > 4096 || num > 256) return list;
            byte[] e = new byte[esz];
            for (uint i = 0; i < num; i++)
            {
                s.Seek(entLba * 512 + (long)i * esz, SeekOrigin.Begin);
                if (Fill(s, e, (int)esz) != (int)esz) break;
                bool empty = true;
                for (int k = 0; k < 16; k++) if (e[k] != 0) { empty = false; break; }
                if (empty) continue;
                long first = BitConverter.ToInt64(e, 32), last = BitConverter.ToInt64(e, 40);
                if (last < first) continue;
                list.Add(new PartInfo { Index = (int)i + 1, Offset = first * 512, Length = (last - first + 1) * 512, Kind = "gpt" });
            }
            return list;
        }
        internal static int Fill(Stream s, byte[] buf, int n)
        {
            int got = 0;
            while (got < n) { int r = s.Read(buf, got, n - got); if (r <= 0) break; got += r; }
            return got;
        }
    }

    public sealed class Ext4
    {
        private readonly Stream s;
        private readonly long baseOff;
        private int bs, inodeSize, descSize; private uint ipg, firstData;
        private bool is64;
        private readonly Dictionary<uint, long> itableCache = new Dictionary<uint, long>();

        private Ext4(Stream s, long baseOff) { this.s = s; this.baseOff = baseOff; }

        // Returns null if there is no ext2/3/4 superblock at baseOff or the volume uses features we cannot read.
        public static Ext4 TryOpen(Stream s, long baseOff, out string why)
        {
            why = null;
            byte[] sb = new byte[1024];
            s.Seek(baseOff + 1024, SeekOrigin.Begin);
            if (PartTable.Fill(s, sb, 1024) != 1024) { why = "short read"; return null; }
            if (BitConverter.ToUInt16(sb, 56) != 0xEF53) { why = "no ext superblock"; return null; }
            var e = new Ext4(s, baseOff);
            e.bs = 1024 << (int)BitConverter.ToUInt32(sb, 24);
            e.ipg = BitConverter.ToUInt32(sb, 40);
            e.firstData = BitConverter.ToUInt32(sb, 20);
            uint rev = BitConverter.ToUInt32(sb, 76);
            e.inodeSize = rev == 0 ? 128 : BitConverter.ToUInt16(sb, 88);
            uint incompat = BitConverter.ToUInt32(sb, 96);
            e.is64 = (incompat & 0x80) != 0;
            e.descSize = e.is64 ? BitConverter.ToUInt16(sb, 254) : 32;
            if (e.descSize < 32) e.descSize = 32;
            if ((incompat & 0x8000) != 0 || (incompat & 0x10000) != 0) { why = "inline_data/encrypted volume not supported"; return null; }
            if (e.bs < 1024 || e.bs > 65536 || e.ipg == 0) { why = "bad superblock"; return null; }
            return e;
        }

        private byte[] ReadAt(long off, int n)
        {
            byte[] b = new byte[n];
            s.Seek(baseOff + off, SeekOrigin.Begin);
            if (PartTable.Fill(s, b, n) != n) throw new IOException("ext4: short read at " + off);
            return b;
        }

        private byte[] Inode(uint ino)
        {
            uint grp = (ino - 1) / ipg, idx = (ino - 1) % ipg;
            long tbl;
            if (!itableCache.TryGetValue(grp, out tbl))
            {
                byte[] gd = ReadAt((long)(firstData + 1) * bs + (long)grp * descSize, descSize);
                tbl = BitConverter.ToUInt32(gd, 8);
                if (is64 && descSize >= 64) tbl |= (long)BitConverter.ToUInt32(gd, 0x28) << 32;
                itableCache[grp] = tbl;
            }
            return ReadAt(tbl * bs + (long)idx * inodeSize, inodeSize);
        }

        private static long Size(byte[] ino)
        {
            long lo = BitConverter.ToUInt32(ino, 4), hi = ino.Length >= 112 ? BitConverter.ToUInt32(ino, 108) : 0;
            return lo | (hi << 32);
        }

        // logical block -> physical block (0 = hole)
        private long MapBlock(byte[] ino, long lblk)
        {
            uint flags = BitConverter.ToUInt32(ino, 32);
            if ((flags & 0x80000) == 0) throw new NotSupportedException("ext4: file without extents (old block map) is not supported");
            return MapExtent(ino, 40, lblk, 0);
        }

        private long MapExtent(byte[] buf, int off, long lblk, int guard)
        {
            if (guard > 8) throw new IOException("ext4: extent tree too deep");
            if (BitConverter.ToUInt16(buf, off) != 0xF30A) throw new IOException("ext4: bad extent header");
            int n = BitConverter.ToUInt16(buf, off + 2), depth = BitConverter.ToUInt16(buf, off + 6);
            if (depth == 0)
            {
                for (int i = 0; i < n; i++)
                {
                    int o = off + 12 + i * 12;
                    uint first = BitConverter.ToUInt32(buf, o); int len = BitConverter.ToUInt16(buf, o + 4);
                    if (len > 32768) len -= 32768;
                    long start = ((long)BitConverter.ToUInt16(buf, o + 6) << 32) | BitConverter.ToUInt32(buf, o + 8);
                    if (lblk >= first && lblk < (long)first + len) return start + (lblk - first);
                }
                return 0;
            }
            int pick = -1;
            for (int i = 0; i < n; i++)
            {
                int o = off + 12 + i * 12;
                if (BitConverter.ToUInt32(buf, o) <= lblk) pick = i; else break;
            }
            if (pick < 0) return 0;
            int po = off + 12 + pick * 12;
            long leaf = ((long)BitConverter.ToUInt16(buf, po + 8) << 32) | BitConverter.ToUInt32(buf, po + 4);
            byte[] node = ReadAt(leaf * bs, bs);
            return MapExtent(node, 0, lblk, guard + 1);
        }

        private byte[] ReadAll(byte[] ino, long max)
        {
            long size = Size(ino);
            if (size > max) throw new IOException("ext4: file larger than " + max + " bytes");
            byte[] data = new byte[size];
            long blocks = (size + bs - 1) / bs;
            for (long b = 0; b < blocks; b++)
            {
                long p = MapBlock(ino, b);
                int take = (int)Math.Min((long)bs, size - b * bs);
                if (p == 0) continue;
                byte[] blk = ReadAt(p * bs, take);
                Buffer.BlockCopy(blk, 0, data, (int)(b * bs), take);
            }
            return data;
        }

        private static int Type(byte[] ino) { return BitConverter.ToUInt16(ino, 0) & 0xF000; }

        private Dictionary<string, uint> ListDir(byte[] ino)
        {
            var d = new Dictionary<string, uint>(StringComparer.Ordinal);
            byte[] data = ReadAll(ino, 64L * 1024 * 1024);
            int p = 0;
            while (p + 8 <= data.Length)
            {
                uint i = BitConverter.ToUInt32(data, p); int rl = BitConverter.ToUInt16(data, p + 4), nl = data[p + 6];
                if (rl < 8 || p + rl > data.Length) { p = (p / bs + 1) * bs; continue; }
                if (i != 0 && nl > 0 && p + 8 + nl <= data.Length)
                    d[Encoding.UTF8.GetString(data, p + 8, nl)] = i;
                p += rl;
            }
            return d;
        }

        private uint Resolve(string path, bool followLast, int depth)
        {
            if (depth > 16) throw new IOException("ext4: too many symlinks");
            uint cur = 2;
            var parts = new List<string>(path.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries));
            var stack = new List<uint>(); stack.Add(2);
            for (int k = 0; k < parts.Count; k++)
            {
                string name = parts[k];
                if (name == ".") continue;
                if (name == "..") { if (stack.Count > 1) stack.RemoveAt(stack.Count - 1); cur = stack[stack.Count - 1]; continue; }
                byte[] dino = Inode(cur);
                if (Type(dino) != 0x4000) return 0;
                uint nxt;
                if (!ListDir(dino).TryGetValue(name, out nxt)) return 0;
                byte[] ni = Inode(nxt);
                bool last = k == parts.Count - 1;
                if (Type(ni) == 0xA000 && (!last || followLast))
                {
                    string target = ReadLink(ni);
                    string rest = string.Join("/", parts.GetRange(k + 1, parts.Count - k - 1).ToArray());
                    string basePath = target.StartsWith("/") ? target : string.Join("/", parts.GetRange(0, k).ToArray()) + "/" + target;
                    return Resolve(basePath + (rest.Length > 0 ? "/" + rest : ""), followLast, depth + 1);
                }
                cur = nxt; stack.Add(cur);
            }
            return cur;
        }

        private string ReadLink(byte[] ino)
        {
            long size = Size(ino);
            if (size < 60 && (BitConverter.ToUInt32(ino, 32) & 0x80000) == 0)
                return Encoding.UTF8.GetString(ino, 40, (int)size);
            return Encoding.UTF8.GetString(ReadAll(ino, 4096));
        }

        public bool Exists(string path) { uint i = Resolve(path, true, 0); return i != 0; }

        // Returns file contents or null if the path does not exist / is not a regular file.
        public byte[] ReadFile(string path, long maxBytes)
        {
            uint i = Resolve(path, true, 0);
            if (i == 0) return null;
            byte[] ino = Inode(i);
            if (Type(ino) != 0x8000) return null;
            return ReadAll(ino, maxBytes);
        }

        public string[] ListNames(string dirPath)
        {
            uint i = Resolve(dirPath, true, 0);
            if (i == 0) return new string[0];
            byte[] ino = Inode(i);
            if (Type(ino) != 0x4000) return new string[0];
            var names = new List<string>(ListDir(ino).Keys);
            names.Sort(StringComparer.Ordinal);
            return names.ToArray();
        }
    }

    public static class Initramfs
    {
        // Returns the member contents, or null. 'why' explains a miss (compression we cannot read, member absent ...).
        public static byte[] TryExtract(string file, string[] memberSuffixes, long maxBytes, out string why)
        {
            why = null;
            using (var fs = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                byte[] head = new byte[64];
                int n = PartTable.Fill(fs, head, 64);
                if (n < 8) { why = "too small"; return null; }
                long skip = 0;
                if (head[0] == 0x27 && head[1] == 0x05 && head[2] == 0x19 && head[3] == 0x56)
                {   // U-Boot legacy uImage: 64-byte header, byte 31 = compression (0 none, 1 gzip)
                    skip = 64;
                    if (head[31] > 1) { why = "uImage compression " + head[31] + " not supported"; return null; }
                    fs.Seek(skip, SeekOrigin.Begin);
                    n = PartTable.Fill(fs, head, 8);
                }
                fs.Seek(skip, SeekOrigin.Begin);
                Stream data;
                if (head[0] == 0x1F && head[1] == 0x8B) data = new GZipStream(fs, CompressionMode.Decompress);
                else if (head[0] == (byte)'0' && head[1] == (byte)'7' && head[2] == (byte)'0' && head[3] == (byte)'7') data = fs;
                else if (head[0] == 0x28 && head[1] == 0xB5 && head[2] == 0x2F && head[3] == 0xFD) { why = "zstd initramfs not supported"; return null; }
                else if (head[0] == 0x04 && head[1] == 0x22 && head[2] == 0x4D && head[3] == 0x18) { why = "lz4 initramfs not supported"; return null; }
                else if (head[0] == 0xFD && head[1] == 0x37 && head[2] == 0x7A && head[3] == 0x58) { why = "xz initramfs not supported"; return null; }
                else { why = "not an initramfs"; return null; }
                try { return FindMember(data, memberSuffixes, maxBytes, out why); }
                catch (InvalidDataException) { why = "corrupt gzip"; return null; }
            }
        }

        private static byte[] FindMember(Stream s, string[] suffixes, long maxBytes, out string why)
        {
            why = null;
            byte[] h = new byte[110];
            long total = 0;
            while (true)
            {
                int got = PartTable.Fill(s, h, 110);
                if (got == 0) break;
                if (got != 110) { why = "truncated cpio"; return null; }
                string magic = Encoding.ASCII.GetString(h, 0, 6);
                if (magic != "070701" && magic != "070702") { why = "not newc cpio"; return null; }
                long size = Convert.ToInt64(Encoding.ASCII.GetString(h, 54, 8), 16);
                int nameLen = (int)Convert.ToInt64(Encoding.ASCII.GetString(h, 94, 8), 16);
                int mode = (int)Convert.ToInt64(Encoding.ASCII.GetString(h, 14, 8), 16);
                byte[] nb = new byte[nameLen];
                if (PartTable.Fill(s, nb, nameLen) != nameLen) { why = "truncated cpio"; return null; }
                string name = Encoding.UTF8.GetString(nb, 0, Math.Max(0, nameLen - 1)).TrimStart('.', '/');
                Skip(s, (4 - ((110 + nameLen) % 4)) % 4);
                if (name == "TRAILER!!!") break;
                bool want = false;
                foreach (string suf in suffixes) if (name == suf.TrimStart('/') || name.EndsWith("/" + suf.TrimStart('/'), StringComparison.Ordinal)) want = true;
                if (want && (mode & 0xF000) == 0x8000)
                {
                    if (size > maxBytes) { why = "member too large"; return null; }
                    byte[] d = new byte[size];
                    if (PartTable.Fill(s, d, (int)size) != size) { why = "truncated cpio"; return null; }
                    return d;
                }
                Skip(s, size + ((4 - (size % 4)) % 4));
                total += size;
            }
            why = "member not found";
            return null;
        }

        private static void Skip(Stream s, long n)
        {
            byte[] buf = new byte[65536];
            while (n > 0) { int r = s.Read(buf, 0, (int)Math.Min((long)buf.Length, n)); if (r <= 0) break; n -= r; }
        }
    }

    public static class Ihex
    {
        public static byte[] ToBin(string text)
        {
            var mem = new SortedDictionary<long, byte>();
            long ext = 0; bool eof = false;
            foreach (string raw in text.Split('\n'))
            {
                string line = raw.Trim();
                if (line.Length == 0) continue;
                if (line[0] != ':') throw new FormatException("not Intel HEX");
                int len = Convert.ToInt32(line.Substring(1, 2), 16);
                int addr = Convert.ToInt32(line.Substring(3, 4), 16);
                int type = Convert.ToInt32(line.Substring(7, 2), 16);
                int sum = 0;
                for (int i = 1; i + 1 < line.Length; i += 2) sum += Convert.ToInt32(line.Substring(i, 2), 16);
                if ((sum & 0xFF) != 0) throw new FormatException("Intel HEX checksum error");
                if (type == 0)
                    for (int i = 0; i < len; i++) mem[ext + addr + i] = (byte)Convert.ToInt32(line.Substring(9 + i * 2, 2), 16);
                else if (type == 1) { eof = true; break; }
                else if (type == 2) ext = (long)Convert.ToInt32(line.Substring(9, 4), 16) << 4;
                else if (type == 4) ext = (long)Convert.ToInt32(line.Substring(9, 4), 16) << 16;
            }
            if (!eof) throw new FormatException("Intel HEX without end record");
            if (mem.Count == 0) return new byte[0];
            long min = long.MaxValue, max = -1;
            foreach (long k in mem.Keys) { if (k < min) min = k; if (k > max) max = k; }
            byte[] bin = new byte[max - min + 1];
            foreach (KeyValuePair<long, byte> kv in mem) bin[kv.Key - min] = kv.Value;
            return bin;
        }
    }
}
