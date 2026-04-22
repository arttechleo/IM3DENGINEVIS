# Stitch six face PNGs (face_PosX.png ...) into a single equirectangular panorama.
# Run from UE editor, or standalone: python3 StitchEquirectangular.py <project_saved_dir>

import math
import os
import subprocess
import sys


def get_project_saved_dir():
    if len(sys.argv) > 1 and sys.argv[1]:
        return os.path.normpath(sys.argv[1])
    import unreal

    return unreal.Paths.project_saved_dir()


def log_info(msg):
    try:
        import unreal

        unreal.log(msg)
    except Exception:
        print(msg, flush=True)


def log_error(msg):
    try:
        import unreal

        unreal.log_error(msg)
    except Exception:
        print(msg, file=sys.stderr, flush=True)


def get_pil_image():
    try:
        from PIL import Image

        return Image
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "Pillow", "-q"])
        from PIL import Image

        return Image


OUT_WIDTH = 2048
OUT_HEIGHT = 1024
STRIP_ROWS = 64

FACE_ORDER = ["PosX", "NegX", "PosY", "NegY", "PosZ", "NegZ"]


def stitch():
    Image = get_pil_image()
    project_saved = get_project_saved_dir()
    faces_dir = os.path.normpath(os.path.join(project_saved, "GreyboxExports", "faces"))
    output_path = os.path.normpath(os.path.join(project_saved, "GreyboxExports", "panorama_360.png"))

    face_imgs = {}
    for name in FACE_ORDER:
        path = os.path.join(faces_dir, "face_%s.png" % name)
        if not os.path.isfile(path):
            log_error("StitchEquirectangular: missing %s" % path)
            return False
        face_imgs[name] = Image.open(path).convert("RGBA")

    face_size = face_imgs["PosX"].width
    face_pixels = {name: face_imgs[name].load() for name in FACE_ORDER}

    output = Image.new("RGBA", (OUT_WIDTH, OUT_HEIGHT))

    for y0 in range(0, OUT_HEIGHT, STRIP_ROWS):
        y1 = min(y0 + STRIP_ROWS, OUT_HEIGHT)
        strip_h = y1 - y0
        strip = Image.new("RGBA", (OUT_WIDTH, strip_h))
        spix = strip.load()

        for local_y in range(strip_h):
            y = y0 + local_y
            phi = math.pi * y / OUT_HEIGHT
            sin_phi = math.sin(phi)
            cos_phi = math.cos(phi)
            for x in range(OUT_WIDTH):
                theta = 2 * math.pi * x / OUT_WIDTH - math.pi
                dx = sin_phi * math.cos(theta)
                dy = sin_phi * math.sin(theta)
                dz = cos_phi

                ax, ay, az = abs(dx), abs(dy), abs(dz)
                if ax >= ay and ax >= az:
                    if dx > 0:
                        name = "PosX"
                        fu = (-dy / ax + 1) / 2
                        fv = (-dz / ax + 1) / 2
                    else:
                        name = "NegX"
                        fu = (dy / ax + 1) / 2
                        fv = (-dz / ax + 1) / 2
                elif ay >= ax and ay >= az:
                    if dy > 0:
                        name = "PosY"
                        fu = (dx / ay + 1) / 2
                        fv = (-dz / ay + 1) / 2
                    else:
                        name = "NegY"
                        fu = (-dx / ay + 1) / 2
                        fv = (-dz / ay + 1) / 2
                elif dz > 0:
                    name = "PosZ"
                    fu = (dx / az + 1) / 2
                    fv = (dy / az + 1) / 2
                else:
                    name = "NegZ"
                    fu = (dx / az + 1) / 2
                    fv = (-dy / az + 1) / 2

                px = min(int(fu * face_size), face_size - 1)
                py = min(int(fv * face_size), face_size - 1)
                spix[x, local_y] = face_pixels[name][px, py]

        output.paste(strip, (0, y0))

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    output.save(output_path)
    log_info("StitchEquirectangular: saved %s" % output_path)
    return True


if __name__ == "__main__":
    ok = stitch()
    sys.exit(0 if ok else 1)
