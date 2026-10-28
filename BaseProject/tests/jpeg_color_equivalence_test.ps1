# 在 Windows 上编译实际转换函数，与优化前的三次 volatile 读取实现逐字节对比。
$ErrorActionPreference = 'Stop'
$compiler = 'D:\QT\Tools\mingw530_32\bin\gcc.exe'
if (!(Test-Path $compiler)) { throw "Missing compiler: $compiler" }
$env:PATH = (Split-Path $compiler) + ';' + $env:PATH
$source = Get-Content (Join-Path $PSScriptRoot '../Utilities/JPEG/jpeg_utils.c') -Raw
$begin = $source.IndexOf('static uint32_t JPEG_ARGB_MCU_YCbCr420_ConvertBlocks (')
$end = $source.IndexOf('/**', $begin)
$optimized = $source.Substring($begin, $end - $begin)
$pattern = 'pixel = \*\(const uint16_t \*\)\(([^;]+)\);\s*red\s*= \(pixel & JPEG_RGB565_RED_MASK\) >> JPEG_RED_OFFSET;\s*green\s*= \(pixel & JPEG_RGB565_GREEN_MASK\) >> JPEG_GREEN_OFFSET;\s*blue\s*= \(pixel & JPEG_RGB565_BLUE_MASK\) >> JPEG_BLUE_OFFSET;'
if ([regex]::Matches($optimized, $pattern).Count -ne 4) { throw 'Expected four optimized pixel reads' }
$reference = [regex]::Replace($optimized, $pattern, {
    param($match)
    $address = $match.Groups[1].Value
    "red = (*(volatile uint16_t *)($address) & JPEG_RGB565_RED_MASK) >> JPEG_RED_OFFSET; green = (*(volatile uint16_t *)($address) & JPEG_RGB565_GREEN_MASK) >> JPEG_GREEN_OFFSET; blue = (*(volatile uint16_t *)($address) & JPEG_RGB565_BLUE_MASK) >> JPEG_BLUE_OFFSET;"
}).Replace('JPEG_ARGB_MCU_YCbCr420_ConvertBlocks', 'reference_convert')
$harness = @'
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define JPEG_RGB565 2
#define JPEG_RGB_FORMAT JPEG_RGB565
#define JPEG_BYTES_PER_PIXEL 2
#define YCBCR_420_BLOCK_SIZE 384
#define JPEG_RGB565_RED_MASK 0xf800
#define JPEG_RGB565_GREEN_MASK 0x07e0
#define JPEG_RGB565_BLUE_MASK 0x001f
#define JPEG_RED_OFFSET 11
#define JPEG_GREEN_OFFSET 5
#define JPEG_BLUE_OFFSET 0
#define CLAMP_8BITS(c) ((c)>255?255:(c))
static struct { uint32_t BlockSize,H_factor,V_factor,WidthExtend,ScaledWidth,LineOffset;
uint16_t *Y_MCU_LUT,*Cb_MCU_LUT,*Cr_MCU_LUT; } JPEG_ConvertorParams;
static int32_t RED_Y_LUT[256],GREEN_Y_LUT[256],BLUE_Y_LUT[256],RED_CB_LUT[256],GREEN_CB_LUT[256],BLUE_CB_RED_CR_LUT[256],GREEN_CR_LUT[256],BLUE_CR_LUT[256];
static uint16_t yl[256],cb[256],cr[256],rgb[800*480];
static uint8_t actual[50*384],expected[50*384];
'@
$main = @'
int main(void) {
 for(int i=0;i<256;i++) {
 RED_Y_LUT[i]=(19595*i+32768)>>16; GREEN_Y_LUT[i]=(38469*i+32768)>>16; BLUE_Y_LUT[i]=(7471*i+32768)>>16;
 RED_CB_LUT[i]=(-11055*i+32768)>>16; GREEN_CB_LUT[i]=(-21712*i+32768)>>16;
 BLUE_CB_RED_CR_LUT[i]=(32768*i+32768)>>16; GREEN_CR_LUT[i]=(-27439*i+32768)>>16; BLUE_CR_LUT[i]=(-5328*i+32768)>>16;
 int x=i%16,y=i/16; yl[i]=(y/8)*128+(x/8)*64+(y%8)*8+x%8; cb[i]=256+(y/2)*8+x/2; cr[i]=cb[i]+64;
 }
 JPEG_ConvertorParams.BlockSize=384; JPEG_ConvertorParams.H_factor=16; JPEG_ConvertorParams.V_factor=16;
 JPEG_ConvertorParams.WidthExtend=800; JPEG_ConvertorParams.ScaledWidth=1600;
 JPEG_ConvertorParams.Y_MCU_LUT=yl; JPEG_ConvertorParams.Cb_MCU_LUT=cb; JPEG_ConvertorParams.Cr_MCU_LUT=cr;
 uint32_t seed=1,a,b;
 for(int trial=0;trial<8;trial++) {
 for(unsigned i=0;i<800*480;i++) { seed=seed*1664525U+1013904223U; rgb[i]=trial==0?i:trial==1?0:trial==2?65535:seed>>16; }
 for(unsigned block=0;block<1500;block+=50) {
 memset(actual,0xa5,sizeof actual); memset(expected,0x5a,sizeof expected);
 uint32_t n=JPEG_ARGB_MCU_YCbCr420_ConvertBlocks((uint8_t*)rgb,actual,block,25600,&a);
 uint32_t m=reference_convert((uint8_t*)rgb,expected,block,25600,&b);
 if(n!=50||m!=50||a!=sizeof actual||b!=a||memcmp(actual,expected,a)) { printf("FAIL trial=%d block=%u\n",trial,block); return 1; }
 }
 }
 puts("PASS RGB565 420: 8 frames, 12000 MCU blocks, byte-identical"); return 0;
}
'@
$directory = Join-Path $HOME ('temp\jpeg-equivalence-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($directory)
try {
    $file = Join-Path $directory 'test.c'
    [IO.File]::WriteAllText($file, $harness + "`n" + $optimized + "`n" + $reference + "`n" + $main)
    $exe = Join-Path $directory 'test.exe'
    & $compiler -std=c99 -O2 $file -o $exe
    if ($LASTEXITCODE) { throw 'Equivalence test build failed' }
    & $exe
    if ($LASTEXITCODE) { throw 'Color conversion mismatch' }
} finally { Remove-Item -LiteralPath $directory -Recurse -Force }
