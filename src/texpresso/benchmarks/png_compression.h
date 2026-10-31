// Experimental PNG writers: preserve the production grayscale scan, filter 0,
// 64 KiB IDAT chunks and resolution metadata. No production dependency changes.
#include <zlib-ng.h>
#include <libdeflate.h>
#include <vector>
#include <stdexcept>
#include <memory>
extern "C" unsigned long crc32(unsigned long, const unsigned char *, unsigned int);

static fz_buffer *compressionPng(fz_context *ctx, fz_pixmap *pix, const QString &mode, size_t *scratch) {
    const bool ng=mode.startsWith("ng");
    const int level=mode.section('-',0,0).mid(ng?2:7).toInt();
    if(level<1 || level>6)throw std::runtime_error("unsupported PNG compression level");
    const bool fastCrc=mode.endsWith("-crc");
    bool grayscale=true;
    for(int y=0;y<pix->h && grayscale;++y) {
        const auto p=pix->samples+size_t(y)*pix->stride;
        for(int x=0;x<pix->w;++x) if(p[3*x]!=p[3*x+1] || p[3*x]!=p[3*x+2]) { grayscale=false;break; }
    }
    const size_t rowSize=size_t(pix->w)*(grayscale?1:3)+1;
    auto output=fz_new_buffer(ctx,65536);
    fz_append_data(ctx,output,"\x89PNG\r\n\x1a\n",8);
    auto integer=[&](unsigned n) { fz_append_int32_be(ctx,output,int32_t(n)); };
    auto chunk=[&](const char *tag,const unsigned char *data,size_t n) {
        integer(n);fz_append_data(ctx,output,tag,4);if(n)fz_append_data(ctx,output,data,n);
        // Keep CRC implementation equal to production to isolate compression.
        auto crc=fastCrc?zng_crc32(0,reinterpret_cast<const unsigned char*>(tag),4):crc32(0,reinterpret_cast<const unsigned char*>(tag),4);
        if(n)crc=fastCrc?zng_crc32(crc,data,n):crc32(crc,data,n);integer(crc);
    };
    unsigned w=pix->w,h=pix->h;
    unsigned char header[13]={static_cast<unsigned char>(w>>24),static_cast<unsigned char>(w>>16),static_cast<unsigned char>(w>>8),static_cast<unsigned char>(w),
        static_cast<unsigned char>(h>>24),static_cast<unsigned char>(h>>16),static_cast<unsigned char>(h>>8),static_cast<unsigned char>(h),8,static_cast<unsigned char>(grayscale?0:2),0,0,0};
    chunk("IHDR",header,13);
    unsigned xr=int(pix->xres*100/2.54f+0.5f),yr=int(pix->yres*100/2.54f+0.5f);
    unsigned char resolution[9]={static_cast<unsigned char>(xr>>24),static_cast<unsigned char>(xr>>16),static_cast<unsigned char>(xr>>8),static_cast<unsigned char>(xr),
        static_cast<unsigned char>(yr>>24),static_cast<unsigned char>(yr>>16),static_cast<unsigned char>(yr>>8),static_cast<unsigned char>(yr),1};
    chunk("pHYs",resolution,9);
    auto fill=[&](unsigned char *row,int y) {
        row[0]=0;const auto p=pix->samples+size_t(y)*pix->stride;
        if(grayscale) {for(int x=0;x<pix->w;++x)row[x+1]=p[3*x];}
        else std::memcpy(row+1,p,rowSize-1);
    };
    if(ng) {
        std::vector<unsigned char> row(rowSize);unsigned char compressed[65536];zng_stream stream{};
        if(zng_deflateInit(&stream,level)!=Z_OK)throw std::runtime_error("zlib-ng init");
        *scratch=rowSize+sizeof compressed;stream.next_out=compressed;stream.avail_out=sizeof compressed;
        for(int y=0;y<pix->h;++y) {
            fill(row.data(),y);stream.next_in=row.data();stream.avail_in=rowSize;
            const bool final=y==pix->h-1;int result;
            do {
                result=zng_deflate(&stream,final?Z_FINISH:Z_NO_FLUSH);
                if(result!=Z_OK && result!=Z_STREAM_END) {zng_deflateEnd(&stream);throw std::runtime_error("zlib-ng compress");}
                if(!stream.avail_out || result==Z_STREAM_END) {
                    chunk("IDAT",compressed,sizeof compressed-stream.avail_out);
                    stream.next_out=compressed;stream.avail_out=sizeof compressed;
                }
            }while(stream.avail_in || (final && result!=Z_STREAM_END));
        }
        zng_deflateEnd(&stream);
    } else {
        auto compressor=libdeflate_alloc_compressor(level);if(!compressor)throw std::bad_alloc();
        const size_t inputBytes=rowSize*pix->h,outputBound=libdeflate_zlib_compress_bound(compressor,inputBytes);
        // Avoid paying for a redundant zero-fill of buffers immediately overwritten.
        std::unique_ptr<unsigned char[]> rows(new unsigned char[inputBytes]),compressed(new unsigned char[outputBound]);
        for(int y=0;y<pix->h;++y)fill(rows.get()+size_t(y)*rowSize,y);
        *scratch=inputBytes+outputBound;
        const auto n=libdeflate_zlib_compress(compressor,rows.get(),inputBytes,compressed.get(),outputBound);
        libdeflate_free_compressor(compressor);if(!n)throw std::runtime_error("libdeflate compress");
        for(size_t offset=0;offset<n;offset+=65536)chunk("IDAT",compressed.get()+offset,std::min(size_t(65536),n-offset));
    }
    chunk("IEND",nullptr,0);return output;
}
