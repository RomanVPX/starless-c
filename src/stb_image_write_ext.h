#ifndef INCLUDE_STB_IMAGE_WRITE_EXT_H
#define INCLUDE_STB_IMAGE_WRITE_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stb_image_write.h"

// Metadata entry for PNG tEXt chunks
typedef struct {
    const char *keyword;    // Maximum 79 bytes
    const char *text;       // Any length
} PngMetadata;

// Write PNG with metadata to memory
unsigned char *stbi_write_png_to_mem_with_metadata(
    const unsigned char *pixels, int stride_bytes, int x, int y, int comp,
    int *out_len,
    const PngMetadata *metadata, int metadata_count
);

// Write PNG with metadata to file
int stbi_write_png_with_metadata(
    char const *filename, int x, int y, int comp, const void *data, int stride_bytes,
    const PngMetadata *metadata, int metadata_count
);

#ifdef __cplusplus
}
#endif

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

unsigned char *stbi_write_png_to_mem_with_metadata(
    const unsigned char *pixels, int stride_bytes, int x, int y, int comp,
    int *out_len,
    const PngMetadata *metadata, int metadata_count)
{
    int i;
    int force_filter = stbi_write_force_png_filter;
    int ctype[5] = { -1, 0, 4, 2, 6 };
    unsigned char sig[8] = { 137,80,78,71,13,10,26,10 };
    unsigned char *out, *o, *filt, *zlib;
    signed char *line_buffer;
    int j,zlen;

    if (stride_bytes == 0)
        stride_bytes = x * comp;

    if (force_filter >= 5) {
        force_filter = -1;
    }

    filt = (unsigned char *) STBIW_MALLOC((x*comp+1) * y); if (!filt) return 0;
    line_buffer = (signed char *) STBIW_MALLOC(x * comp); if (!line_buffer) { STBIW_FREE(filt); return 0; }
    for (j=0; j < y; ++j) {
        int filter_type;
        if (force_filter > -1) {
            filter_type = force_filter;
            stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, comp, force_filter, line_buffer);
        } else {
            // Try each filter type, take the best one
            int best_filter = 0, best_filter_val = 0x7fffffff, est, i;
            for (filter_type = 0; filter_type < 5; filter_type++) {
                stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, comp, filter_type, line_buffer);

                // Estimate the entropy of the line using this filter
                est = 0;
                for (i = 0; i < x*comp; ++i) {
                    est += abs((signed char) line_buffer[i]);
                }
                if (est < best_filter_val) {
                    best_filter_val = est;
                    best_filter = filter_type;
                }
            }
            if (filter_type != best_filter) {  // If the last iteration already got us the best filter, don't redo it
                stbiw__encode_png_line((unsigned char*)(pixels), stride_bytes, x, y, j, comp, best_filter, line_buffer);
                filter_type = best_filter;
            }
        }
        filt[j*(x*comp+1)] = (unsigned char) filter_type;
        STBIW_MEMMOVE(filt+j*(x*comp+1)+1, line_buffer, x*comp);
    }
    STBIW_FREE(line_buffer);
    zlib = stbi_zlib_compress(filt, y*( x*comp+1), &zlen, stbi_write_png_compression_level);
    STBIW_FREE(filt);
    if (!zlib) return 0;

    // Calculate total length including metadata
    int total_meta_len = 0;
    for(i = 0; i < metadata_count; i++) {
        total_meta_len += 12 + strlen(metadata[i].keyword) + 1 + strlen(metadata[i].text);
    }

    // each tag requires 12 bytes of overhead
    out = (unsigned char *) STBIW_MALLOC(8 + 12+13 + 12+zlen + 12 + total_meta_len);
    if (!out) return 0;
    *out_len = 8 + 12+13 + 12+zlen + 12 + total_meta_len;

    o=out;
    STBIW_MEMMOVE(o,sig,8); o+= 8;
    stbiw__wp32(o, 13); // header length
    stbiw__wptag(o, "IHDR");
    stbiw__wp32(o, x);
    stbiw__wp32(o, y);
    *o++ = 8;
    *o++ = STBIW_UCHAR(ctype[comp]);
    *o++ = 0;
    *o++ = 0;
    *o++ = 0;
    stbiw__wpcrc(&o,13);

    // Write metadata chunks
    for(i = 0; i < metadata_count; i++) {
        int keyword_len = strlen(metadata[i].keyword);
        int text_len = strlen(metadata[i].text);
        int chunk_len = keyword_len + 1 + text_len;

        // Write length
        o[0] = (chunk_len >> 24) & 0xff;
        o[1] = (chunk_len >> 16) & 0xff;
        o[2] = (chunk_len >> 8) & 0xff;
        o[3] = chunk_len & 0xff;

        // Write chunk type
        o[4] = 't';
        o[5] = 'E';
        o[6] = 'X';
        o[7] = 't';
        o += 8;

        // Write data
        memcpy(o, metadata[i].keyword, keyword_len);
        o += keyword_len;
        *o++ = 0;
        memcpy(o, metadata[i].text, text_len);
        o += text_len;

        // Calculate and write CRC
        unsigned char *temp = (unsigned char*)STBIW_MALLOC(chunk_len + 4);
        memcpy(temp, "tEXt", 4);
        memcpy(temp + 4, metadata[i].keyword, keyword_len);
        temp[4 + keyword_len] = 0;
        memcpy(temp + 4 + keyword_len + 1, metadata[i].text, text_len);
        unsigned int crc = stbiw__crc32(temp, chunk_len + 4);
        STBIW_FREE(temp);

        o[0] = (crc >> 24) & 0xff;
        o[1] = (crc >> 16) & 0xff;
        o[2] = (crc >> 8) & 0xff;
        o[3] = crc & 0xff;
        o += 4;
    }

    stbiw__wp32(o, zlen);
    stbiw__wptag(o, "IDAT");
    STBIW_MEMMOVE(o, zlib, zlen);
    o += zlen;
    STBIW_FREE(zlib);
    stbiw__wpcrc(&o, zlen);

    stbiw__wp32(o,0);
    stbiw__wptag(o, "IEND");
    stbiw__wpcrc(&o,0);

    STBIW_ASSERT(o == out + *out_len);

    return out;
}

int stbi_write_png_with_metadata(char const *filename, int x, int y, int comp, const void *data, int stride_bytes,
    const PngMetadata *metadata, int metadata_count)
{
    FILE *f;
    int len;
    unsigned char *png = stbi_write_png_to_mem_with_metadata((const unsigned char *) data, stride_bytes, x, y, comp, &len,
        metadata, metadata_count);
    if (png == NULL) return 0;

    f = stbiw__fopen(filename, "wb");
    if (!f) { STBIW_FREE(png); return 0; }
    fwrite(png, 1, len, f);
    fclose(f);
    STBIW_FREE(png);
    return 1;
}

#endif // STB_IMAGE_WRITE_IMPLEMENTATION
#endif // INCLUDE_STB_IMAGE_WRITE_EXT_H
