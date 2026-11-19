import re, sys
# Resume las lineas [recorrido] de un vsrunner.log: salas ordenadas por frames perdidos y coste
pat = re.compile(r"\[recorrido\] \[(?P<tag>[^\]]+)\] (?P<i>\d+)/(?P<n>\d+) (?P<room>\S+) \| (?P<fr>\d+) frames, (?P<lost>\d+) perdidos \((?P<pct>[\d.]+)%\) \| cpu media (?P<cpu>[\d.]+) ms, max (?P<cpumax>[\d.]+) \| logica (?P<lg>[\d.]+)/(?P<lgmax>[\d.]+) \| dibujo (?P<dr>[\d.]+)/(?P<drmax>[\d.]+) \| peor frame (?P<worst>[\d.]+) ms \| entrada (?P<entry>\d+) ms \| instancias (?P<inst>\d+)")
rows = []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = pat.search(line)
    if m: rows.append(m.groupdict())
for r in rows:
    for k in ('fr', 'lost', 'inst', 'entry'): r[k] = int(r[k])
    for k in ('pct', 'cpu', 'cpumax', 'lg', 'lgmax', 'dr', 'drmax', 'worst'): r[k] = float(r[k])
n = len(rows)
clean = [r for r in rows if r['lost'] == 0]
print(f"salas medidas: {n}; sin ningun frame perdido: {len(clean)}; con perdidas: {n - len(clean)}")
tot = sum(r['fr'] for r in rows); lost = sum(r['lost'] for r in rows)
print(f"frames totales {tot}, perdidos {lost} ({100.0 * lost / max(tot, 1):.2f}%)")
print("\nPeores salas (por % de frames perdidos, luego cpu media):")
print(f"{'sala':16} {'perd%':>6} {'cpu':>5} {'cpuMax':>6} {'logica':>6} {'dibujo':>6} {'peor':>6} {'entrada':>7} {'inst':>5}")
for r in sorted(rows, key=lambda r: (-r['pct'], -r['cpu']))[:30]:
    print(f"{r['room']:16} {r['pct']:6.1f} {r['cpu']:5.1f} {r['cpumax']:6.1f} {r['lg']:6.1f} {r['dr']:6.1f} {r['worst']:6.1f} {r['entry']:7d} {r['inst']:5d}")
print("\nMas cargadas por cpu media (margen hasta 16,7 ms):")
for r in sorted(rows, key=lambda r: -r['cpu'])[:15]:
    print(f"{r['room']:16} cpu {r['cpu']:5.1f} ms (logica {r['lg']:.1f} + dibujo {r['dr']:.1f}) margen {16.7 - r['cpu']:5.1f} ms, inst {r['inst']}")
print("\nEntradas a sala mas lentas:")
for r in sorted(rows, key=lambda r: -r['entry'])[:10]:
    print(f"{r['room']:16} entrada {r['entry']} ms")
