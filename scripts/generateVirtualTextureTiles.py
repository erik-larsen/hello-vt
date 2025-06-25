#!/usr/bin/env python
#
#  generateVirtualTextureTiles.py
#  VirtualTexturing
#
#  Created by Julian Mayer on 29.07.09.
#  Copyright (c) 2010 A. Julian Mayer
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitationthe rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# pip install Pillow

import sys, os

border = 0
crop = 0
resize = 0
tileSize = 256
format = "png"

def print_exit(s): 
    print (s)
    sys.exit()


try:
    from PIL import Image
except:
    print_exit("Error: Python Imaging Library or later not found")

Image.MAX_IMAGE_PIXELS = None

f = sys.argv[len(sys.argv) - 1]
print(f)
im = Image.open(f)

try:
    for i in range(1, len(sys.argv) - 1):
        print (i, sys.argv[i])
        if sys.argv[i].startswith("-t="):      tileSize = int(sys.argv[i][3:])
        elif sys.argv[i].startswith("-f="):    format = sys.argv[i][3:]
        elif sys.argv[i].startswith("-b="):    border = int(sys.argv[i][3:])
        elif sys.argv[i].startswith("-c="):    crop = int(sys.argv[i][3:])
        elif sys.argv[i].startswith("-r="):    resize = int(sys.argv[i][3:])
        else:                                  raise Exception('input', 'error')
except:
    print_exit("""Usage: %s [options] image_file
Options:
  -b=<border>\t  Produce tiles with a border <border> pixels wide (Default: 0)
  -t=<tile_size>  Set the tile size to <64, 128, 256, 512> (Default: 256)
  -f=<format>\t  Set the tile format to <bmp, jpg, png> (Default: png)""" % (sys.argv[0]))


if (tileSize not in [64, 128, 256, 512]):
    print_exit("Error: tile_size must be 64, 128, 256 or 512")

if border:    tileSize -= (border * 2)
if crop:    im = im.crop(((im.size[0] - crop) / 2, (im.size[1] - crop) / 2, ((im.size[0] - crop) / 2) + crop, ((im.size[1] - crop) / 2) + crop))
if resize:    im = im.resize((resize, resize), Image.Resampling.LANCZOS)

if (im.size[0] != im.size[1]) or (im.size[0] not in [i * tileSize for i in [32, 64, 128, 256, 512, 1024]]):
    print_exit("Error: input image must be in: " + ', '.join([str(i * tileSize) + "^2" for i in [32, 64, 128, 256, 512, 1024]]))


# Add total tiles calculation and counter
total_tiles = 0
current_tile = 0

# After size calculation
size = int(im.size[0])
times = 0
levels = 0
temp_size = size
while temp_size >= tileSize:
    total_tiles += (temp_size // tileSize) ** 2
    temp_size //= 2

print(f"Processing image of size {size}x{size}")
print(f"Will generate {total_tiles} tiles across multiple levels")

tileDir = os.path.splitext(f)[0]
if not os.path.exists(tileDir):
    os.mkdir(tileDir)

while size >= tileSize:
    print(f"\nProcessing level {times} ({size}x{size})")
    if (times != 0):
        print("Resizing image...")
        im = im.resize((size, size), Image.Resampling.LANCZOS)

    baseDir = os.path.splitext(f)[0] + "/tiles_b" + str(border) + "_level"  + str(times)
    if not os.path.exists(baseDir):
        os.mkdir(baseDir)
    len = int(size / tileSize)

    for x in range(len):
        for y in range(len):
            current_tile += 1
            if current_tile % 100 == 0:  # Show progress every 100 tiles
                print(f"Progress: {current_tile}/{total_tiles} tiles ({(current_tile/total_tiles)*100:.1f}%)")
            
            if (not border):
                part = im.crop((x * tileSize , y * tileSize , x * tileSize + tileSize , y * tileSize + tileSize))
            else:
                part = im.crop((x * tileSize - border, y * tileSize - border, x * tileSize + tileSize + border, y * tileSize + tileSize + border))
                if (x == 0):        part.paste(part.crop((border, 0, border*2, tileSize + border*2)), (0, 0, border, tileSize + border*2))
                if (y == 0):        part.paste(part.crop((0, border, tileSize + border*2, border*2)), (0, 0, tileSize + border*2, border))
                if (x == len-1):    part.paste(part.crop((tileSize, 0, tileSize + border, tileSize + border*2)), (tileSize + border, 0, tileSize + border*2, tileSize + border*2))
                if (y == len-1):    part.paste(part.crop((0, tileSize, tileSize + border*2, tileSize + border)), (0, tileSize + border, tileSize + border*2, tileSize + border*2))

            part.save(baseDir + "/" + "tile_" + str(times) + "_" + str(x) + "_" + str(y) + "." + format)

    times += 1
    size //= 2

print(f"\nComplete! Generated {current_tile} tiles")
