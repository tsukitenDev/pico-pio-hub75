from PIL import Image


def correct_gamma(blightness, gamma=2.2):
    return 255 * pow((blightness / 255), gamma)


def printlist(dots, height, width, start="", end=""):
    '''
    1ピクセル16ビット
    '''
    s = ""
    s += start
    for y in range(height):
        for x in range(width):
            rgb = dots[y][x]
            #リトルエンディアンなので逆順に
            s += format(rgb & 0xFF, '#04x') + ', ' + format(rgb >> 8, '#04x') + ', '
            if (y == height - 1) and (x == width-1):
                s = s[:-2]
            if (x+1)%8==0:
                s += "\n"
        s += "\n"
    s += end
    s += "\n"
    return s


def printlist32(dots, height, width, start="", end=""):
    '''
    1ピクセル32ビット
    '''
    s = ""
    s += start
    for y in range(height):
        for x in range(width):
            rgb = dots[y][x]
            #リトルエンディアンなので逆順に
            s += format((rgb & 0x000000FF) >> 0, '#04x') + ', '
            s += format((rgb & 0x0000FF00) >> 8, '#04x') + ', '
            s += format((rgb & 0x00FF0000) >> 16, '#04x')  + ', '
            s += format((rgb & 0xFF000000) >> 24, '#04x')  + ', '
            if (y == height - 1) and (x == width-1):
                s = s[:-2]
            if (x+1)%4==0:
                s += "\n"
        s += "\n"
    s += end
    s += "\n"
    return s





def convert_color(rgb_im, gamma=1, is_2byte=False):
    res = [[0] * rgb_im.size[0] for _ in range(rgb_im.size[1])]
    for x in range(rgb_im.size[0]):
        for y in range(rgb_im.size[1]):
            r, g, b = rgb_im.getpixel((x,y))
            r = int(correct_gamma(r, gamma))
            g = int(correct_gamma(g, gamma))
            b = int(correct_gamma(b, gamma))

            if is_2byte:
                r = r >> 3
                g = g >> 2
                b = b >> 3
                res[y][x]  = (r << 11) | (g << 5) | b
            else:
                res[y][x]  = (r << 16) | (g << 8) | b
    return res


if __name__ == '__main__':
    input_path = 'fujisan_64x32.png'
    output_path = 'img2.h'

    im = Image.open(input_path)
    rgb_im = im.convert('RGB')

    size = rgb_im.size
    print(size)
    HEIGHT = size[1]
    WIDTH = size[0]
    #一次元に展開
    dots = [[0] * (WIDTH) for _ in range(HEIGHT)]

    rgb_dots = convert_color(rgb_im, gamma=1)
    print(hex(rgb_dots[2][0]))
    print(rgb_im.getpixel((0,2)))

    with open(output_path, encoding='utf-8', mode='w') as f:
        f.write(printlist32(rgb_dots, HEIGHT, WIDTH, start="static char __attribute__((aligned(4))) img_64x32[] = {\n", end="};"))

