// Valdis Story Vita - exportador data.win (GMS1, bytecode 13) -> formato VSD propio.
// Se ejecuta con UndertaleModCli:  UndertaleModCli load data.win -s export_vsdata.csx
// Variable de entorno VSD_OUT = carpeta de salida (game.vsd + tex/*.png + snd/*.wav)
//
// Formato game.vsd (little endian). Ver runner/src/data/vsd.h para el lector.
//   magic "VSD1", u32 version
//   secciones en orden fijo; cadenas = u32 len + bytes UTF-8
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using UndertaleModLib.Models;

EnsureDataLoaded();
string outDir = Environment.GetEnvironmentVariable("VSD_OUT");
if (string.IsNullOrEmpty(outDir)) throw new Exception("VSD_OUT no definido");
Directory.CreateDirectory(outDir);
Directory.CreateDirectory(Path.Combine(outDir, "tex"));
Directory.CreateDirectory(Path.Combine(outDir, "snd"));

var bw = new BinaryWriter(File.Create(Path.Combine(outDir, "game.vsd")));
void WS(string s) { var b = Encoding.UTF8.GetBytes(s ?? ""); bw.Write((uint)b.Length); bw.Write(b); }
int Idx<T>(IList<T> list, T item) where T : class => item == null ? -1 : list.IndexOf(item);

bw.Write(Encoding.ASCII.GetBytes("VSD1"));
bw.Write((uint)1);

// ---------- general
var gen = Data.GeneralInfo;
WS(gen.Name?.Content);
bw.Write(gen.DefaultWindowWidth); bw.Write(gen.DefaultWindowHeight);
bw.Write((uint)gen.RoomOrder.Count);
foreach (var r in gen.RoomOrder) bw.Write(Idx(Data.Rooms, r.Resource));

// ---------- strings
var strIndex = new Dictionary<UndertaleString, int>();
bw.Write((uint)Data.Strings.Count);
for (int i = 0; i < Data.Strings.Count; i++) { strIndex[Data.Strings[i]] = i; WS(Data.Strings[i].Content); }

// ---------- texture pages (PNG files)
bw.Write((uint)Data.EmbeddedTextures.Count);
for (int i = 0; i < Data.EmbeddedTextures.Count; i++)
{
    var t = Data.EmbeddedTextures[i];
    using (var fs = File.Create(Path.Combine(outDir, "tex", i + ".png"))) t.TextureData.Image.SavePng(fs);
    bw.Write(t.TextureData.Image.Width); bw.Write(t.TextureData.Image.Height);
}

// ---------- texture page items
bw.Write((uint)Data.TexturePageItems.Count);
foreach (var p in Data.TexturePageItems)
{
    bw.Write(p.SourceX); bw.Write(p.SourceY); bw.Write(p.SourceWidth); bw.Write(p.SourceHeight);
    bw.Write(p.TargetX); bw.Write(p.TargetY); bw.Write(p.TargetWidth); bw.Write(p.TargetHeight);
    bw.Write(p.BoundingWidth); bw.Write(p.BoundingHeight);
    bw.Write(Idx(Data.EmbeddedTextures, p.TexturePage));
}
int Tp(UndertaleTexturePageItem p) => Idx(Data.TexturePageItems, p);

// ---------- sprites
bw.Write((uint)Data.Sprites.Count);
foreach (var s in Data.Sprites)
{
    WS(s.Name.Content);
    bw.Write(s.Width); bw.Write(s.Height);
    bw.Write(s.OriginX); bw.Write(s.OriginY);
    bw.Write(s.MarginLeft); bw.Write(s.MarginRight); bw.Write(s.MarginTop); bw.Write(s.MarginBottom);
    bw.Write(s.BBoxMode); bw.Write((uint)s.SepMasks);
    bw.Write((uint)s.Textures.Count);
    foreach (var te in s.Textures) bw.Write(Tp(te.Texture));
    bw.Write((uint)s.CollisionMasks.Count);
    foreach (var m in s.CollisionMasks) { bw.Write(m.Width); bw.Write(m.Height); bw.Write((uint)m.Data.Length); bw.Write(m.Data); }
}

// ---------- backgrounds
bw.Write((uint)Data.Backgrounds.Count);
foreach (var b in Data.Backgrounds) { WS(b.Name.Content); bw.Write(Tp(b.Texture)); bw.Write(b.Transparent); bw.Write(b.Smooth); }

// ---------- fonts
bw.Write((uint)Data.Fonts.Count);
foreach (var f in Data.Fonts)
{
    WS(f.Name.Content); WS(f.DisplayName?.Content);
    bw.Write(f.EmSize); bw.Write(f.Bold); bw.Write(f.Italic);
    bw.Write(Tp(f.Texture)); bw.Write(f.ScaleX); bw.Write(f.ScaleY);
    bw.Write((uint)f.Glyphs.Count);
    foreach (var g in f.Glyphs)
    {
        bw.Write(g.Character); bw.Write(g.SourceX); bw.Write(g.SourceY); bw.Write(g.SourceWidth); bw.Write(g.SourceHeight);
        bw.Write(g.Shift); bw.Write(g.Offset);
    }
}

// ---------- sounds (+ embedded audio)
bw.Write((uint)Data.EmbeddedAudio.Count);
for (int i = 0; i < Data.EmbeddedAudio.Count; i++) File.WriteAllBytes(Path.Combine(outDir, "snd", i + ".wav"), Data.EmbeddedAudio[i].Data);
bw.Write((uint)Data.Sounds.Count);
foreach (var s in Data.Sounds)
{
    WS(s.Name.Content); bw.Write((uint)s.Flags); WS(s.Type?.Content); WS(s.File?.Content);
    bw.Write(s.Volume); bw.Write(s.Pitch); bw.Write(s.AudioID);
    bw.Write(Idx(Data.EmbeddedAudio, s.AudioFile));
}

// ---------- variable names (unique) and function names
var varNames = new List<string>(); var varId = new Dictionary<string, int>();
int VarName(string n) { if (!varId.TryGetValue(n, out int id)) { id = varNames.Count; varNames.Add(n); varId[n] = id; } return id; }
foreach (var v in Data.Variables) VarName(v.Name.Content);
bw.Write((uint)Data.Functions.Count); foreach (var f in Data.Functions) WS(f.Name.Content);

// ---------- code
// Instruccion VSD (20 bytes): u8 op, u8 t1, u8 t2, u8 cmp, i16 inst, u8 extra, u8 flags, i32 a, i32 b, f64-index/aux i32 c
var dbl = new List<double>(); var dblIdx = new Dictionary<long, int>();
int Dbl(double d) { long k = BitConverter.DoubleToInt64Bits(d); if (!dblIdx.TryGetValue(k, out int i)) { i = dbl.Count; dbl.Add(d); dblIdx[k] = i; } return i; }
var funcIdx = new Dictionary<UndertaleFunction, int>();
for (int i = 0; i < Data.Functions.Count; i++) funcIdx[Data.Functions[i]] = i;

var codeBlob = new MemoryStream(); var cw = new BinaryWriter(codeBlob);
int unknownOps = 0;
cw.Write((uint)Data.Code.Count);
foreach (var code in Data.Code)
{
    var ins = code.Instructions;
    var addrToIndex = new Dictionary<uint, int>();
    uint addr = 0;
    var addrs = new uint[ins.Count];
    for (int i = 0; i < ins.Count; i++) { addrs[i] = addr; addrToIndex[addr] = i; addr += ins[i].CalculateInstructionSize(); }
    addrToIndex[addr] = ins.Count; // salto al final = salir
    cw.Write(Encoding.UTF8.GetBytes(code.Name.Content).Length); cw.Write(Encoding.UTF8.GetBytes(code.Name.Content));
    cw.Write((uint)code.ArgumentsCount); cw.Write(code.LocalsCount);
    cw.Write((uint)ins.Count);
    for (int i = 0; i < ins.Count; i++)
    {
        var x = ins[i];
        byte op = (byte)x.Kind;
        byte t1 = (byte)x.Type1, t2 = (byte)x.Type2, cmp = (byte)x.ComparisonKind;
        short inst = 0; byte extra = 0, flags = 0; int a = 0, b = 0;
        switch (x.Kind)
        {
            case UndertaleInstruction.Opcode.B:
            case UndertaleInstruction.Opcode.Bt:
            case UndertaleInstruction.Opcode.Bf:
            case UndertaleInstruction.Opcode.PushEnv:
            case UndertaleInstruction.Opcode.PopEnv:
                if (x.Kind == UndertaleInstruction.Opcode.PopEnv && x.JumpOffsetPopenvExitMagic) { flags = 1; a = -1; }
                else
                {
                    uint target = (uint)(addrs[i] + x.JumpOffset);
                    if (!addrToIndex.TryGetValue(target, out a)) throw new Exception($"{code.Name.Content}: salto invalido {i}");
                }
                break;
            case UndertaleInstruction.Opcode.Push:
            case UndertaleInstruction.Opcode.PushLoc:
            case UndertaleInstruction.Opcode.PushGlb:
            case UndertaleInstruction.Opcode.PushBltn:
            case UndertaleInstruction.Opcode.PushI:
                switch (x.Type1)
                {
                    case UndertaleInstruction.DataType.Double: a = Dbl(x.ValueDouble); break;
                    case UndertaleInstruction.DataType.Int64: a = Dbl((double)x.ValueLong); break;
                    case UndertaleInstruction.DataType.Int32:
                        if (x.ValueFunction != null) { flags = 2; a = funcIdx[x.ValueFunction]; }
                        else a = x.ValueInt; break;
                    case UndertaleInstruction.DataType.Int16: a = x.ValueShort; break;
                    case UndertaleInstruction.DataType.String: a = strIndex[x.ValueString.Resource]; break;
                    case UndertaleInstruction.DataType.Variable:
                        inst = (short)x.TypeInst; a = VarName(x.ValueVariable.Name.Content); b = (int)x.ReferenceType; break;
                    default: a = x.ValueInt; break;
                }
                break;
            case UndertaleInstruction.Opcode.Pop:
                if (x.Type1 == UndertaleInstruction.DataType.Int16) { flags = 4; extra = x.Extra; b = x.SwapExtra; }
                else { inst = (short)x.TypeInst; a = VarName(x.ValueVariable.Name.Content); b = (int)x.ReferenceType; }
                break;
            case UndertaleInstruction.Opcode.Call:
                a = funcIdx[x.ValueFunction]; b = x.ArgumentsCount; break;
            case UndertaleInstruction.Opcode.Dup:
                extra = x.Extra; b = (byte)(((int)x.ComparisonKind & 0x7F) >> 3); break;
            case UndertaleInstruction.Opcode.Break:
                a = x.ValueShort; break;
            case UndertaleInstruction.Opcode.Conv: case UndertaleInstruction.Opcode.Mul: case UndertaleInstruction.Opcode.Div:
            case UndertaleInstruction.Opcode.Rem: case UndertaleInstruction.Opcode.Mod: case UndertaleInstruction.Opcode.Add:
            case UndertaleInstruction.Opcode.Sub: case UndertaleInstruction.Opcode.And: case UndertaleInstruction.Opcode.Or:
            case UndertaleInstruction.Opcode.Xor: case UndertaleInstruction.Opcode.Neg: case UndertaleInstruction.Opcode.Not:
            case UndertaleInstruction.Opcode.Shl: case UndertaleInstruction.Opcode.Shr: case UndertaleInstruction.Opcode.Cmp:
            case UndertaleInstruction.Opcode.Ret: case UndertaleInstruction.Opcode.Exit: case UndertaleInstruction.Opcode.Popz:
                break;
            default: unknownOps++; break;
        }
        cw.Write(op); cw.Write(t1); cw.Write(t2); cw.Write(cmp);
        cw.Write(inst); cw.Write(extra); cw.Write(flags);
        cw.Write(a); cw.Write(b);
    }
}
cw.Flush();
// variables may have grown while encoding code: rewrite the table after the code
bw.Write((uint)dbl.Count); foreach (var d in dbl) bw.Write(d);
bw.Write(codeBlob.ToArray());
bw.Write((uint)varNames.Count); foreach (var n in varNames) WS(n);

// ---------- scripts
bw.Write((uint)Data.Scripts.Count);
foreach (var s in Data.Scripts) { WS(s.Name.Content); bw.Write(Idx(Data.Code, s.Code)); }

// ---------- objects
int nonCode = 0;
bw.Write((uint)Data.GameObjects.Count);
foreach (var o in Data.GameObjects)
{
    WS(o.Name.Content);
    bw.Write(Idx(Data.Sprites, o.Sprite)); bw.Write(o.Visible); bw.Write(o.Solid); bw.Write(o.Depth); bw.Write(o.Persistent);
    bw.Write(Idx(Data.GameObjects, o.ParentId)); bw.Write(Idx(Data.Sprites, o.TextureMaskId));
    var evs = new List<(int type, uint sub, int code)>();
    for (int t = 0; t < o.Events.Count; t++)
        foreach (var e in o.Events[t])
        {
            int codeId = -1;
            foreach (var act in e.Actions)
            {
                if (act.CodeId != null) codeId = Idx(Data.Code, act.CodeId);
                else nonCode++;
            }
            evs.Add((t, e.EventSubtype, codeId));
        }
    bw.Write((uint)evs.Count);
    foreach (var e in evs) { bw.Write(e.type); bw.Write(e.sub); bw.Write(e.code); }
}

// ---------- rooms
bw.Write((uint)Data.Rooms.Count);
foreach (var r in Data.Rooms)
{
    WS(r.Name.Content); WS(r.Caption?.Content);
    bw.Write(r.Width); bw.Write(r.Height); bw.Write(r.Speed); bw.Write(r.Persistent);
    bw.Write(r.BackgroundColor); bw.Write(r.DrawBackgroundColor); bw.Write(Idx(Data.Code, r.CreationCodeId));
    bw.Write((uint)r.Flags);
    bw.Write((uint)r.Backgrounds.Count);
    foreach (var b in r.Backgrounds)
    {
        bw.Write(b.Enabled); bw.Write(b.Foreground); bw.Write(Idx(Data.Backgrounds, b.BackgroundDefinition));
        bw.Write(b.X); bw.Write(b.Y); bw.Write(b.TiledHorizontally); bw.Write(b.TiledVertically);
        bw.Write(b.SpeedX); bw.Write(b.SpeedY); bw.Write(b.Stretch);
    }
    bw.Write((uint)r.Views.Count);
    foreach (var v in r.Views)
    {
        bw.Write(v.Enabled); bw.Write(v.ViewX); bw.Write(v.ViewY); bw.Write(v.ViewWidth); bw.Write(v.ViewHeight);
        bw.Write(v.PortX); bw.Write(v.PortY); bw.Write(v.PortWidth); bw.Write(v.PortHeight);
        bw.Write(v.BorderX); bw.Write(v.BorderY); bw.Write(v.SpeedX); bw.Write(v.SpeedY);
        bw.Write(Idx(Data.GameObjects, v.ObjectId));
    }
    bw.Write((uint)r.GameObjects.Count);
    foreach (var g in r.GameObjects)
    {
        bw.Write(g.X); bw.Write(g.Y); bw.Write(Idx(Data.GameObjects, g.ObjectDefinition)); bw.Write(g.InstanceID);
        bw.Write(Idx(Data.Code, g.CreationCode)); bw.Write(g.ScaleX); bw.Write(g.ScaleY); bw.Write(g.Color); bw.Write(g.Rotation);
    }
    bw.Write((uint)r.Tiles.Count);
    foreach (var t in r.Tiles)
    {
        bw.Write(t.X); bw.Write(t.Y); bw.Write(Idx(Data.Backgrounds, t.BackgroundDefinition));
        bw.Write(t.SourceX); bw.Write(t.SourceY); bw.Write(t.Width); bw.Write(t.Height);
        bw.Write(t.TileDepth); bw.Write(t.InstanceID); bw.Write(t.ScaleX); bw.Write(t.ScaleY); bw.Write(t.Color);
    }
}
bw.Write(Encoding.ASCII.GetBytes("END!"));
bw.Close();
ScriptMessage($"VSD exportado: {Data.Code.Count} codigos, {varNames.Count} variables, {dbl.Count} doubles, ops desconocidos={unknownOps}, acciones no-codigo={nonCode}");
