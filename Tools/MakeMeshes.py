"""Build low-poly drone and pickup OBJ source meshes. Dimensions use centimetres."""
from pathlib import Path
from math import cos, sin, tau

OUT = Path(__file__).resolve().parents[1] / "ArtSource"
OUT.mkdir(exist_ok=True)

class Mesh:
    def __init__(self):
        self.points, self.faces = [], []

    def hull(self, polygon, z0, z1, inset=0.72):
        start = len(self.points) + 1
        cx = sum(p[0] for p in polygon) / len(polygon)
        cy = sum(p[1] for p in polygon) / len(polygon)
        n = len(polygon)
        for z, scale in ((z0, 1), (z1, inset)):
            self.points.extend((cx + (x-cx)*scale, cy + (y-cy)*scale, z) for x,y in polygon)
        self.faces.append(tuple(start+i for i in reversed(range(n))))
        self.faces.append(tuple(start+n+i for i in range(n)))
        for i in range(n):
            j = (i+1) % n
            self.faces.append((start+i, start+j, start+n+j, start+n+i))

    def ring(self, x, y, r, z0, z1, top, n=6):
        self.hull([(x+r*cos(tau*i/n), y+r*sin(tau*i/n)) for i in range(n)], z0, z1, top)

    def save(self, name):
        # OBJ importer reads Y-up. Emit X-forward, Y-up, Z-right.
        lines = [f"o {name}"] + [f"v {x:.3f} {z:.3f} {y:.3f}" for x,y,z in self.points]
        lines += ["f " + " ".join(map(str, reversed(face))) for face in self.faces]
        (OUT / (name+".obj")).write_text("\n".join(lines)+"\n", encoding="ascii")

ship = Mesh()
ship.hull([(52,0),(-18,20),(-32,12),(-32,-12),(-18,-20)], -6, 12)
for y in (-25,25):
    ship.ring(-12,y,16,-6,5,0.80,8)
    ship.ring(-12,y,8,5,12,0.60,8)
ship.save("Scout")
foe = Mesh()
foe.hull([(37,0),(-20,22),(-28,0),(-20,-22)], -9, 12)
for y in (-21,21):
    foe.hull([(27,y),(-17,y+9),(-18,y-7)], -5, 6)
foe.save("Hunter")
sentry = Mesh()
sentry.ring(0,0,30,-9,10,0.7,8)
for y in (-16,16):
    sentry.hull([(42,y-4),(42,y+4),(-8,y+5),(-8,y-5)], 0, 9)
sentry.save("Lancer")
gem = Mesh()
gem.ring(0,0,12,-2,19,0.1,5)
gem.save("Crystal")
