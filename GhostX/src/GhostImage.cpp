#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "../include/stb_image.h"
#include "../include/GhostImage.h"
#include <cstdio>

bool GhostImage_Load(const std::string& utf8Path, unsigned int maxSize,
    std::vector<unsigned char>& rgba, unsigned int& w, unsigned int& h)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, GhostUtf8ToWide(utf8Path).c_str(), L"rb") != 0 || !f) return false;

    int iw = 0, ih = 0, comp = 0;
    unsigned char* px = stbi_load_from_file(f, &iw, &ih, &comp, 4);
    fclose(f);
    if (!px || iw <= 0 || ih <= 0) { if (px) stbi_image_free(px); return false; }

    if (maxSize == 0 || maxSize > 4096) maxSize = 4096;
    const unsigned int sw = (unsigned int)iw, sh = (unsigned int)ih;

    if (sw <= maxSize && sh <= maxSize)
    {
        rgba.assign(px, px + (size_t)sw * sh * 4);
        w = sw; h = sh;
    }
    else
    {
        double s = (double)maxSize / (double)(sw > sh ? sw : sh);
        unsigned int nw = (unsigned int)(sw * s); if (nw < 1) nw = 1;
        unsigned int nh = (unsigned int)(sh * s); if (nh < 1) nh = 1;
        rgba.resize((size_t)nw * nh * 4);

        for (unsigned int y = 0; y < nh; y++)
        {
            unsigned int y0 = (unsigned int)((unsigned long long)y * sh / nh);
            unsigned int y1 = (unsigned int)((unsigned long long)(y + 1) * sh / nh);
            if (y1 <= y0) y1 = y0 + 1;
            for (unsigned int x = 0; x < nw; x++)
            {
                unsigned int x0 = (unsigned int)((unsigned long long)x * sw / nw);
                unsigned int x1 = (unsigned int)((unsigned long long)(x + 1) * sw / nw);
                if (x1 <= x0) x1 = x0 + 1;

                unsigned int sum[4] = { 0, 0, 0, 0 }, cnt = 0;
                for (unsigned int yy = y0; yy < y1; yy++)
                    for (unsigned int xx = x0; xx < x1; xx++)
                    {
                        const unsigned char* p = px + ((size_t)yy * sw + xx) * 4;
                        sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3];
                        cnt++;
                    }
                unsigned char* o = &rgba[((size_t)y * nw + x) * 4];
                for (int k = 0; k < 4; k++) o[k] = (unsigned char)(sum[k] / cnt);
            }
        }
        w = nw; h = nh;
    }
    stbi_image_free(px);
    return true;
}