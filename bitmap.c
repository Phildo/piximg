#include "bitmap.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "pix.h"
#include "str.h"

#define READFIELD(field) ({ if(sizeof(field) != fread(&field, sizeof(byte), sizeof(field), fp)) { sprintf(err->info, "Error reading field"); return ERR; } })
#define WRITEFIELD(field) ({ if(sizeof(field) != fwrite(&field, sizeof(byte), sizeof(field), fp)) { sprintf(err->info, "Error reading field"); return ERR; } })

static ERR_EXISTS readFileHeader(FILE *fp, BitmapFileHeader *bfh, PixErr *err)
{
  READFIELD(bfh->header_field);
  if(cmp((char *)bfh->header_field,"BM")) ERROR("File not valid Bitmap"); //can cmp because bfh ought be 0'd before use
  READFIELD(bfh->size);
  if(bfh->size <= 0) ERROR("Filesize invalid");
  READFIELD(bfh->reserved_a);
  READFIELD(bfh->reserved_b);
  READFIELD(bfh->offset);
  if(bfh->size <= 0) ERROR("Data offset invalid");

  return NO_ERR;
}

static ERR_EXISTS writeFileHeader(FILE *fp, BitmapFileHeader *bfh, PixErr *err)
{
  fwrite("BM", sizeof(char), 2, fp);
  WRITEFIELD(bfh->size);
  WRITEFIELD(bfh->reserved_a);
  WRITEFIELD(bfh->reserved_b);
  WRITEFIELD(bfh->offset);

  return NO_ERR;
}

static ERR_EXISTS readDIBHeader(FILE *fp, DIBHeader *dh, PixErr *err)
{
  READFIELD(dh->header_size);
  BITMAPINFOHEADER *ih = &dh->bitmap_info_header;
  BITMAPV5HEADER *v5h = &dh->bitmap_v5_header;
  switch(dh->header_size)
  {
    case BITMAPINFOHEADER_SIZE:
    case BITMAPV5HEADER_SIZE:
      break;
    case BITMAPCOREHEADER_SIZE:   ERROR("Unsupported DIB header CORE (header size %d)\n",  dh->header_size); break;
    case OS22XBITMAPHEADER_SIZE:  ERROR("Unsupported DIB header OS22X (header size %d)\n", dh->header_size); break;
    case BITMAPV2INFOHEADER_SIZE: ERROR("Unsupported DIB header V2 (header size %d)\n",    dh->header_size); break;
    case BITMAPV3INFOHEADER_SIZE: ERROR("Unsupported DIB header V3 (header size %d)\n",    dh->header_size); break;
    case BITMAPV4HEADER_SIZE:     ERROR("Unsupported DIB header V4 (header size %d)\n",    dh->header_size); break;
    default:                      ERROR("Unsupported DIB header (header size %d)\n",       dh->header_size); break;
  }
  READFIELD(ih->width);
  READFIELD(ih->height);
  READFIELD(ih->nplanes);
  READFIELD(ih->bpp);
  READFIELD(ih->compression);
  if(!(ih->compression == 0 || ih->compression == 3)) ERROR("Unable to process compressed bitmaps");
  READFIELD(ih->image_size);
  if(!(ih->image_size == 0 || ih->image_size >= ih->width*abs(ih->height)*(ih->bpp/8))) ERROR("Unable to process compressed bitmaps");
  READFIELD(ih->horiz_resolution);
  READFIELD(ih->vert_resolution);
  READFIELD(ih->ncolors);
  if(ih->ncolors != 0) ERROR("Unable to process indexed bitmaps");
  READFIELD(ih->nimportantcolors);

  if(dh->header_size == BITMAPV5HEADER_SIZE && ih->compression == 3)
  {
    READFIELD(v5h->bV5RedMask);
    READFIELD(v5h->bV5GreenMask);
    READFIELD(v5h->bV5BlueMask);
    if(ih->bpp == 32) READFIELD(v5h->bV5AlphaMask);
    else v5h->bV5AlphaMask = 0;
  }

  return NO_ERR;
}

static ERR_EXISTS writeDIBHeader(FILE *fp, DIBHeader *dh, PixErr *err)
{
  WRITEFIELD(dh->header_size);
  BITMAPINFOHEADER *ih = &dh->bitmap_info_header;
  BITMAPV5HEADER *v5h = &dh->bitmap_v5_header;
  WRITEFIELD(ih->width);
  WRITEFIELD(ih->height);
  WRITEFIELD(ih->nplanes);
  WRITEFIELD(ih->bpp);
  WRITEFIELD(ih->compression);
  WRITEFIELD(ih->image_size);
  WRITEFIELD(ih->horiz_resolution);
  WRITEFIELD(ih->vert_resolution);
  WRITEFIELD(ih->ncolors);
  WRITEFIELD(ih->nimportantcolors);

  WRITEFIELD(v5h->bV5RedMask);
  WRITEFIELD(v5h->bV5GreenMask);
  WRITEFIELD(v5h->bV5BlueMask);
  WRITEFIELD(v5h->bV5AlphaMask);

  return NO_ERR;
}

static ERR_EXISTS readColorTable(FILE *fp, byte *b, int size, PixErr *err)
{
  return NO_ERR;
}

static ERR_EXISTS readGap(FILE *fp, byte *b, int size, PixErr *err)
{
  return NO_ERR;
}

static ERR_EXISTS readPixelArray(FILE *fp, byte *b, int size, PixErr *err)
{
  if(size != fread(b, sizeof(byte), size, fp)) ERROR("Can't read specified length at offset in bitmap");
  return NO_ERR;
}

static ERR_EXISTS writePixelArray(FILE *fp, byte *b, int size, PixErr *err)
{
  if(size != fwrite(b, sizeof(byte), size, fp)) ERROR("Can't write specified length at offset in bitmap");
  return NO_ERR;
}

static ERR_EXISTS readColorProfile(FILE *fp, byte *b, int size, PixErr *err)
{
  return NO_ERR;
}

ERR_EXISTS readBitmap(const char *infile, Bitmap *b, PixErr *err)
{
  //INPUT
  FILE *in;
  if(!(in = fopen(infile, "r"))) ERROR("Can't open input file- %s",infile);

  InternalBitmap *simple = &b->simple;

  BitmapFileHeader *bh = &b->bitmap_file_header;
  if(!readFileHeader(in, bh, err)) { fclose(in); return ERR; }

  DIBHeader *dh = &b->dib_header;
  if(!readDIBHeader(in, dh, err)) { fclose(in); return ERR; }

  //put together useful info in reading the rest
  simple->width = dh->bitmap_info_header.width;
  simple->height = abs(dh->bitmap_info_header.height);
  simple->reversed = dh->bitmap_info_header.height < 0;
  simple->bpp = dh->bitmap_info_header.bpp;
  simple->row_w = ((simple->bpp*simple->width+31)/32)*4;
  simple->pixel_n_bytes = simple->row_w*simple->height;
  simple->offset_to_data = bh->offset;
  simple->compression = dh->bitmap_v5_header.bV5Compression;
  simple->r_mask = dh->bitmap_v5_header.bV5RedMask;
  simple->g_mask = dh->bitmap_v5_header.bV5GreenMask;
  simple->b_mask = dh->bitmap_v5_header.bV5BlueMask;
  simple->a_mask = dh->bitmap_v5_header.bV5AlphaMask;

  if(dh->bitmap_info_header.ncolors > 0)
  {
    fclose(in); ERROR("Unable to process indexed bitmaps- shouldn't have gotten here");
    byte *ct = b->color_table;
    if(!readColorTable(in, ct, 0, err)) { fclose(in); return ERR; }
  }

  //does nothing...
  byte *g = b->gap1;
  if(!readGap(in, g, 0, err)) { fclose(in); return ERR; }

  if(fseek(in, simple->offset_to_data, SEEK_SET) == -1) ERROR("Unable to read bitmap file");
  b->pixel_array = calloc(simple->pixel_n_bytes,1);
  if(!b->pixel_array) ERROR("Out of memory");
  byte *pa = b->pixel_array;
  if(!readPixelArray(in, pa, simple->pixel_n_bytes, err)) { fclose(in); return ERR; }

  //does nothing...
  g = b->gap2;
  if(!readGap(in, g, 0, err)) { fclose(in); return ERR; }

  //does nothing...
  byte *cp = b->icc_color_profile;
  if(!readColorProfile(in, cp, 0, err)) { fclose(in); return ERR; }

  fclose(in);

  return NO_ERR;
}

ERR_EXISTS writeBitmap(const char *outname, Bitmap *b, PixErr *err)
{
  char out_file[2048];
  sprintf(out_file,"%s.bmp",outname);
  InternalBitmap *simple = &b->simple;

  FILE *out;
  FILE *fp;
  if(!(out = fopen(out_file, "w"))) ERROR("Can't open output file- %s",out_file);
  fp = out;

  BitmapFileHeader *bh = &b->bitmap_file_header;
  bh->size = simple->offset_to_data+simple->pixel_n_bytes;
  bh->reserved_a = 0;//dunno, normally 0
  bh->reserved_b = 0;//dunno, normally 0
  bh->offset = simple->offset_to_data;
  if(!writeFileHeader(out, bh, err)) { fclose(out); return ERR; }

  DIBHeader *dh = &b->dib_header;
  dh->header_size = BITMAPV5HEADER_SIZE;

  BITMAPINFOHEADER *ih = &dh->bitmap_info_header;
  BITMAPV5HEADER *v5h = &dh->bitmap_v5_header;
  ih->width = simple->width;
  ih->height = simple->height;
  ih->nplanes = 1;
  ih->bpp = 32;
  ih->compression = 3;
  ih->image_size = simple->width*simple->height*4;
  ih->horiz_resolution = 0;//was 2835 with 32x32 bmp...
  ih->vert_resolution = 0;//was 2835 with 32x32 bmp...
  ih->ncolors = 0;//experimentally. but don't understand...
  ih->nimportantcolors = 0;//experimentally. but don't understand...

  v5h->bV5RedMask   = 0x00FF0000;
  v5h->bV5GreenMask = 0x0000FF00;
  v5h->bV5BlueMask  = 0x000000FF;
  v5h->bV5AlphaMask = 0xFF000000;

  if(!writeDIBHeader(out, dh, err)) { fclose(out); return ERR; }

  if(fseek(out, simple->offset_to_data, SEEK_SET) == -1) ERROR("Unable to write bitmap file");
  if(!writePixelArray(out, b->pixel_array, simple->pixel_n_bytes, err)) { fclose(out); return ERR; }

  fclose(out);

  return NO_ERR;
}

ERR_EXISTS readBitField(const char *infile, BitField *b, PixErr *err)
{
  char n_parse_buff[100];
  int pbuff_i = 0;

  int parse_i = 0;
  while(infile[parse_i] != '.' && infile[parse_i] != '\0') parse_i++;
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no . extension found)",infile);
  parse_i++;
  pbuff_i = 0;
  while(infile[parse_i] != 'x' && infile[parse_i] != '\0') { n_parse_buff[pbuff_i] = infile[parse_i]; pbuff_i++; parse_i++; }
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no x found in extension)",infile);
  n_parse_buff[parse_i] = '\0';
  b->width = atoi(n_parse_buff);
  parse_i++;
  pbuff_i = 0;
  while(infile[parse_i] != 'p' && infile[parse_i] != '\0') { n_parse_buff[pbuff_i] = infile[parse_i]; pbuff_i++; parse_i++; }
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no p found in extension)",infile);
  n_parse_buff[parse_i] = '\0';
  b->height = atoi(n_parse_buff);

  FILE *in;
  if(!(in = fopen(infile, "r"))) ERROR("Can't open input file- %s",infile);

  b->bytes = (int)(ceil((b->width*b->height)/8.)+0.01);
  b->data = calloc(b->bytes*sizeof(byte),1);
  if(!b->data) ERROR("Out of memory");

  if(!readPixelArray(in, b->data, b->bytes, err)) { fclose(in); return ERR; }

  fclose(in);

  return NO_ERR;
}

ERR_EXISTS writeBitField(const char *out_name, BitField *b, PixErr *err)
{
  char out_file[2048];
  sprintf(out_file,"%s.%dx%dbf",out_name,b->width,b->height);

  FILE *out;
  if(!(out = fopen(out_file, "w"))) ERROR("Can't open output file- %s",out_file);

  fwrite(b->data, sizeof(byte), b->bytes, out);

  fclose(out);
  return NO_ERR;
}

ERR_EXISTS readPixImg(const char *infile, PixImg *img, PixErr *err)
{
  char n_parse_buff[100];
  int pbuff_i = 0;

  int parse_i = 0;
  while(infile[parse_i] != '.' && infile[parse_i] != '\0') parse_i++;
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no . extension found)",infile);
  parse_i++;
  pbuff_i = 0;
  while(infile[parse_i] != 'x' && infile[parse_i] != '\0') { n_parse_buff[pbuff_i] = infile[parse_i]; pbuff_i++; parse_i++; }
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no x found in extension)",infile);
  n_parse_buff[parse_i] = '\0';
  img->width = atoi(n_parse_buff);
  parse_i++;
  pbuff_i = 0;
  while(infile[parse_i] != 'p' && infile[parse_i] != '\0') { n_parse_buff[pbuff_i] = infile[parse_i]; pbuff_i++; parse_i++; }
  if(infile[parse_i] == '\0') ERROR("Filename %s invalid piximage file- expected blah.#x#pi (no p found in extension)",infile);
  n_parse_buff[parse_i] = '\0';
  img->height = atoi(n_parse_buff);

  FILE *in;
  if(!(in = fopen(infile, "r"))) ERROR("Can't open input file- %s",infile);

  int n_bytes = img->width*img->height*4;
  byte *pa = calloc(n_bytes*sizeof(byte),1);
  if(!pa) ERROR("Out of memory");

  if(!readPixelArray(in, pa, n_bytes, err)) { fclose(in); return ERR; }

  img->data = calloc(img->width*img->height*sizeof(Pix),1);
  if(!img->data) ERROR("Out of memory");

  for(int y = 0; y < img->height; y++)
  {
    for(int x = 0; x < img->width; x++)
    {
      int index = ((y*img->width)+x);
      img->data[index].r = pa[index*4+0];
      img->data[index].g = pa[index*4+1];
      img->data[index].b = pa[index*4+2];
      img->data[index].a = pa[index*4+3];
    }
  }

  free(pa);
  fclose(in);

  return NO_ERR;
}

ERR_EXISTS writePixImg(const char *out_name, PixImg *img, PixErr *err)
{
  char out_file[2048];
  sprintf(out_file,"%s.%dx%dpi",out_name,img->width,img->height);

  int array_size = img->width*img->height*4;
  byte *array = calloc(array_size,1);
  if(!array) ERROR("Out of memory");

  for(int y = 0; y < img->height; y++)
  {
    for(int x = 0; x < img->width; x++)
    {
      int index = ((y*img->width)+x);
      array[index*4+0] = img->data[index].r;
      array[index*4+1] = img->data[index].g;
      array[index*4+2] = img->data[index].b;
      array[index*4+3] = img->data[index].a;
    }
  }

  FILE *out;
  if(!(out = fopen(out_file, "w"))) ERROR("Can't open output file- %s",out_file);

  fwrite(array, sizeof(byte), array_size, out);

  fclose(out);

  free(array);

  return NO_ERR;
}

byte maskMap(uint32 mask)
{
  switch(mask)
  {
    case 0xff000000: return 3; break;
    case 0x00ff0000: return 2; break;
    case 0x0000ff00: return 1; break;
    case 0x000000ff: return 0; break;
    default: return 255; break;
  }
  return 255;
}

static ERR_EXISTS dataToPix(Bitmap *b, PixImg *img, PixErr *err)
{
  byte *data = b->pixel_array;
  int roww = b->simple.row_w;

  byte rmask = 0;
  byte gmask = 0;
  byte bmask = 0;
  byte amask = 0;

  switch(b->simple.bpp)
  {
    case 32:
      rmask = 3;
      gmask = 2;
      bmask = 1;
      amask = 0;
      if(b->simple.compression == 0)
      {
        rmask = 2;
        gmask = 1;
        bmask = 0;
        amask = 3;
      }
      else if(b->simple.compression == 3)
      {
        rmask = maskMap(b->simple.r_mask);
        gmask = maskMap(b->simple.g_mask);
        bmask = maskMap(b->simple.b_mask);
        amask = maskMap(b->simple.a_mask);
      }

      if(b->simple.reversed)
      {
        for(int i = 0; i < img->height; i++)
        {
          for(int j = 0; j < img->width; j++)
          {
            img->data[(i*img->width)+j].a = (amask == 255) ? 255 : data[(i*roww)+(j*4)+amask];
            img->data[(i*img->width)+j].b = (bmask == 255) ? 255 : data[(i*roww)+(j*4)+bmask];
            img->data[(i*img->width)+j].g = (gmask == 255) ? 255 : data[(i*roww)+(j*4)+gmask];
            img->data[(i*img->width)+j].r = (rmask == 255) ? 255 : data[(i*roww)+(j*4)+rmask];
          }
        }
      }
      else
      {
        for(int i = 0; i < img->height; i++)
        {
          for(int j = 0; j < img->width; j++)
          {
            img->data[(i*img->width)+j].a = (amask == 255) ? 255 : data[((img->height-1-i)*roww)+(j*4)+amask];
            img->data[(i*img->width)+j].b = (bmask == 255) ? 255 : data[((img->height-1-i)*roww)+(j*4)+bmask];
            img->data[(i*img->width)+j].g = (gmask == 255) ? 255 : data[((img->height-1-i)*roww)+(j*4)+gmask];
            img->data[(i*img->width)+j].r = (rmask == 255) ? 255 : data[((img->height-1-i)*roww)+(j*4)+rmask];
          }
        }
      }
    break;
    case 24:
      rmask = 2;
      gmask = 1;
      bmask = 0;
      amask = 255;
      if(b->simple.compression == 3)
      {
        rmask = maskMap(b->simple.r_mask);
        gmask = maskMap(b->simple.g_mask);
        bmask = maskMap(b->simple.b_mask);
        amask = maskMap(b->simple.a_mask);
      }

      for(int i = 0; i < img->height; i++)
      {
        for(int j = 0; j < img->width; j++)
        {
          img->data[(i*img->width)+j].a = (amask == 255) ? 255 : data[( (b->simple.reversed ? i : (img->height-1-i) ) *roww)+(j*3)+bmask];
          img->data[(i*img->width)+j].b = (bmask == 255) ? 255 : data[( (b->simple.reversed ? i : (img->height-1-i) ) *roww)+(j*3)+bmask];
          img->data[(i*img->width)+j].g = (gmask == 255) ? 255 : data[( (b->simple.reversed ? i : (img->height-1-i) ) *roww)+(j*3)+gmask];
          img->data[(i*img->width)+j].r = (rmask == 255) ? 255 : data[( (b->simple.reversed ? i : (img->height-1-i) ) *roww)+(j*3)+rmask];
        }
      }
    break;
    default:
      ERROR("Error parsing unsupported bpp");
      break;
  }
  return NO_ERR;
}

static ERR_EXISTS pixToData(PixImg *img, Bitmap *b, PixErr *err)
{
  byte *data = b->pixel_array;

  b->simple.reversed = 0;
  b->simple.bpp = 32;
  b->simple.row_w = b->simple.width*4;
  b->simple.pixel_n_bytes = b->simple.row_w*b->simple.height;
  b->simple.offset_to_data = 138; //experimentally...
  b->simple.compression = 3;
  //experimentally w/ compression = 3
  b->simple.r_mask = 2;
  b->simple.g_mask = 1;
  b->simple.b_mask = 0;
  b->simple.a_mask = 3;

  int w = b->simple.width;
  int h = b->simple.height;

  int rmask = b->simple.r_mask;
  int gmask = b->simple.g_mask;
  int bmask = b->simple.b_mask;
  int amask = b->simple.a_mask;

  for(int i = 0; i < h; i++)
  {
    for(int j = 0; j < w; j++)
    {
      data[((h-1-i)*w*4)+(j*4)+amask] = img->data[(i*w)+j].a;
      data[((h-1-i)*w*4)+(j*4)+bmask] = img->data[(i*w)+j].b;
      data[((h-1-i)*w*4)+(j*4)+gmask] = img->data[(i*w)+j].g;
      data[((h-1-i)*w*4)+(j*4)+rmask] = img->data[(i*w)+j].r;
    }
  }

  return NO_ERR;
}

ERR_EXISTS bitmapToImage(Bitmap *b, PixImg *img, PixErr *err)
{
  img->width  = b->simple.width;
  img->height = b->simple.height;
  img->data = calloc(img->width*img->height*sizeof(Pix),1);
  if(!img->data) ERROR("Out of memory");
  if(!dataToPix(b, img, err)) return ERR;
  return NO_ERR;
}

ERR_EXISTS imageToBitmap(PixImg *img, Bitmap *b, PixErr *err)
{
  b->simple.width = img->width;
  b->simple.height = img->height;
  b->pixel_array = calloc(b->simple.width*b->simple.height*sizeof(byte)*4,1);
  if(!b->pixel_array) ERROR("Out of memory");
  if(!pixToData(img, b, err)) return ERR;
  return NO_ERR;
}

ERR_EXISTS bitFieldToImage(BitField *b, PixImg *img, PixErr *err)
{
  img->width = b->width;
  img->height = b->height;
  img->data = calloc(img->width*img->height*sizeof(Pix),1);
  if(!img->data) ERROR("Out of memory");

  for(int i = 0; i < img->height; i++)
  {
    for(int j = 0; j < img->width; j++)
    {
      int index = (i*img->width)+j;
      int byte_i = index/8;
      int bit_i = index-(byte_i*8);
      byte d = b->data[byte_i] | (1 << (8-1-bit_i));
      if(d) img->data[index].a = 1;
    }
  }

  return NO_ERR;
}

ERR_EXISTS imageToBitField(PixImg *img, BitField *b, PixErr *err)
{
  b->bytes = (int)(ceil((img->width*img->height)/8.)+0.01);
  b->width = img->width;
  b->height = img->height;
  b->data = calloc(b->bytes,1);
  if(!b->data) ERROR("Out of memory");

  for(int i = 0; i < img->height; i++)
  {
    for(int j = 0; j < img->width; j++)
    {
      int index = (i*img->width)+j;
      int byte_i = index/8;
      int bit_i = index-(byte_i*8);
      if(img->data[index].r == 0)
      {
        b->data[byte_i] |= (1 << (8-1-bit_i));
        printf("1");
      }
      else
        printf("0");
    }
    printf("\n");
  }

  return NO_ERR;
}

