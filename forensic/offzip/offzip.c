/*
    Copyright 2004-2019 Luigi Auriemma

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

    http://www.gnu.org/licenses/gpl-2.0.txt
*/

//#define NOLFS
#ifndef NOLFS   // 64 bit file support not really needed since the tool uses signed 32 bits at the moment, anyway I leave it enabled
    #define _LARGE_FILES        // if it's not supported the tool will work
    #define __USE_LARGEFILE64   // without support for large files
    #define __USE_FILE_OFFSET64
    #define _LARGEFILE_SOURCE
    #define _LARGEFILE64_SOURCE
    #define _FILE_OFFSET_BITS   64
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <zlib.h>
#include "zopfli/zopfli.h"

#ifdef WIN32
    #include <direct.h>
    #include <windows.h>    // for recursive_dir
    #define PATHSLASH   '\\'
#else
    #include <dirent.h>
    #define stricmp     strcasecmp
    #define strnicmp    strncasecmp
    #define PATHSLASH   '/'
#endif

#if defined(_LARGE_FILES)
    #if defined(__APPLE__)
        #define fseek   fseeko
        #define ftell   ftello
    #elif defined(__FreeBSD__)
    #elif !defined(NOLFS)       // use -DNOLFS if this tool can't be compiled on your OS!
        #define off_t   off64_t
        #define fopen   fopen64
        #define fseek   fseeko64
        #define ftell   ftello64
        #ifndef fstat
            #ifdef WIN32
                #define fstat   _fstati64
                #define stat    _stati64
            #else
                #define fstat   fstat64
                #define stat    stat64
            #endif
        #endif
    #endif
#endif

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;



// use FREE instead of free
#define FREE(X)         if(X) { \
                            free(X); \
                            X = NULL; \
                        }
#define PATH_DELIMITERS     "\\/"



#include "sign_ext.c"



#define VER             "0.4.1"
#define INSZ            0x800   // the amount of bytes we want to decompress each time
#define OUTSZ           0x10000 // the buffer used for decompressing the data
#define FBUFFSZ         0x40000 // this buffer is used for reading, faster
#define SHOWX           0x7ff   // AND to show the current scanned offset each SHOWX offsets

enum {
    ZIPDOSCAN2,
    ZIPDOSCAN,
    ZIPDODUMP,
    ZIPDODUMP2,
    ZIPDOFILE,
    ZIPDOERROR
};

#define Z_INIT_ERROR    -1000
#define Z_END_ERROR     -1001
#define Z_RESET_ERROR   -1002

#define MAXZIPLEN(n) ((n)+(((n)/1000)+1)+12)

    #define PRId            PRId64
    #define PRIu            PRIu64
    #define PRIx            "08"PRIx64 //"016"PRIx64



typedef struct {
    u8      *name;
    //u64     offset; // unused at the moment
    u64     size;
} files_t;



int offzip(u8 *file_input, u8 *file_output, u8 *file_offset, int zipdo, FILE **fdo);
u8 *mystrrchrs(u8 *str, u8 *chrs);
u8 *get_filename(u8 *fname);
int make_dir(u8 *folder);
int check_is_dir(u8 *fname);
files_t *add_files(u8 *fname, u64 fsize, int *ret_files);
int recursive_dir(u8 *filedir, int filedirsz);
int buffread(FILE *fd, u8 *buff, int size);
void buffseek(FILE *fd, u64 off, int mode);
void buffinc(int increase);
int zip_search(FILE *fd);
int unzip_all(FILE *fd, FILE **fdo, int zipdo);
int unzip(FILE *fd, FILE **fdo, u64 *inlen, u64 *outlen, int zipdo, u8 *dumpname);
void offzip_show_dump(int before_after, unsigned char *data, unsigned int len, FILE *stream);
u64 get_num(u8 *str);
void zlib_err(int err);
u8 *fdloadx(u8 *fname, u64 *fsize, FILE *fd_in);
FILE *save_file(u8 *fname, int dump);
void myfw(u8 *buff, int size, FILE *fd);
void FCLOSE(FILE **fd);
void std_err(void);



z_stream    z;
FILE    *g_fdl          = NULL;
u64     g_total_zsize   = 0,
        g_total_size    = 0,
        g_offset        = 0,
        g_filebuffoff   = 0,
        g_filebuffsz    = 0,
        g_dictionarysz  = 0,
        g_last_offset   = 0;
u64     g_vidsump       = 0;
int     g_zipwbits      = 15,
        g_minzip        = 32,
        g_quiet         = 0,
        g_reminval      = 1,
        g_only_one      = 0,
        g_hexview       = 0,
        g_chunks        = 0,
        g_reimport      = 0,
        g_reimported    = 0,
        g_overwrite     = 0,
        g_try_output_dir= 1;
u8      *g_in           = NULL,
        *g_out          = NULL,
        *g_filebuff     = NULL,
        *g_listfile     = NULL,
        *g_dictionary   = NULL,
        *g_only_one_name= NULL,
        **g_filter_in_files = NULL,     // the wildcard
        *g_folder_name  = NULL;



int main(int argc, char *argv[]) {
    FILE    *fdo            = NULL;
    int     i,
            zipdo           = ZIPDOFILE;
    u8      *file_input     = NULL,
            *file_output    = NULL,
            *file_offset    = 0;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    #ifdef O_BINARY
    setmode(fileno(stdin), O_BINARY);
    #endif

    fputs("\n"
        "Offzip "VER"\n"
        "by Luigi Auriemma\n"
        "e-mail: aluigi@autistici.org\n"
        "web:    aluigi.org\n"
        "\n", stdout);

    if(argc < 2) {
        printf("\n"
            "Usage: %s [options] <input/dir> [output/dir] [offset]\n"
            "\n"
            "Options:\n"
            "-s       scan for one compressed data in the input from the specified offset\n"
            "-S       as above but continues the scan, just like -a without extraction\n"
            "-a       extracts all the compressed data found in the input file into the\n"
            "         specified output folder, each output file is identified by the offset\n"
            "         of where the data was located\n"
            "-A       as above but without decompressing the data, just dumped \"as-is\"\n"
            "-1       related to -a/-A, generates one unique output file instead of many\n"
            "-m SIZE  minimum compressed size to check for valid data. default is %d, use a\n"
            "         higher value to reduce the false positives or a smaller one (eg 16) to\n"
            "         see very small compressed data too\n"
            "-z NUM   this option sets the windowBits value:\n"
            "         -z  15 = zlib data (default) zlib is header+deflate+crc\n"
            "         -z -15 = deflate data (many false positives, used in ZIP archives)\n"
            "-q       quiet, all the verbose error messages will be suppressed (-Q for more)\n"
            "-R       do not remove the invalid uncompressed files generated with -a and -A\n"
            "-x       visualization of hexadecimal numbers\n"
            "-L FILE  dump the list of \"0xoffset zsize size\" into the specified FILE\n"
            "-D FD    use a dictionary from file FD\n"
            "-d N     hex dump of N bytes before and after the compressed stream\n"
            "-c N     experimental guessing of files splitted in chunks where N is the max\n"
            "         uncompressed size of the chunks, note that the non-compressed chunks\n"
            "         can be recognized only if the chunks are sequential, requires -a\n"
            "-o       overwrite existent files without asking\n"
            "-r       reimport mode that works EXACTLY like in QuickBMS\n"
            "\n"
            "Note: Offset is a decimal number or a hex number if you use the 0x prefix, for\n"
            "      example: 1234 or 0x4d2\n"
            "      The displayed zlib info are CM, CINFO, FCHECK, FDICT, FLEVEL, ADLER32.\n"
            "\n"
            "Quick examples:\n"
            "  scan zlib        offzip -S input.dat\n"
            "  scan offset      offzip -S input.dat 0 0x12345678\n"
            "  cool scan zlib   offzip -S -x -Q input.dat\n"
            "  extract zlib     offzip -a input.dat c:\\output\n"
            "  extract deflate  offzip -a -z -15 -Q input.dat c:\\output\n"
            "  reimport zlib    offzip -a -r file.dat c:\\input\n"
            "\n", argv[0], g_minzip);
        exit(1);
    }

    argc -= 1;
    for(i = 1; i < argc; i++) {
        if(((argv[i][0] != '-') && (argv[i][0] != '/')) || (strlen(argv[i]) != 2)) {
            break;
            //printf("\nError: recheck your options, %s is not valid\n", argv[i]);
            //exit(1);
        }
        switch(argv[i][1]) {
            case 's': zipdo         = ZIPDOSCAN2;           break;
            case 'S': zipdo         = ZIPDOSCAN;            break;
            case 'a': zipdo         = ZIPDODUMP;            break;
            case 'A': zipdo         = ZIPDODUMP2;           break;
            case '1': g_only_one    = 1;                    break;
            case 'm': g_minzip      = get_num(argv[++i]);   break;
            case 'z':
                i++;
                     if(!stricmp(argv[i], "zlib"))    g_zipwbits = 15;
                else if(!stricmp(argv[i], "deflate")) g_zipwbits = -15;
                else                                  g_zipwbits = atoi(argv[i]);
                break;
            case 'q': g_quiet       = 1;                    break;
            case 'Q': g_quiet       = -1;                   break;
            case 'R': g_reminval    = 0;                    break;
            case 'x': g_hexview     = 1;                    break;
            case 'L': g_listfile    = argv[++i];            break;
            case 'D': g_dictionary  = fdloadx(argv[++i], &g_dictionarysz, NULL); break;
            case 'd': g_vidsump     = get_num(argv[++i]);   break;
            case 'c': g_chunks      = get_num(argv[++i]);   break;
            case 'o': g_overwrite   = 1;                    break;
            case 'r': g_reimport    = 1;                    break;
            default: {
                printf("\nError: wrong command-line argument (%s)\n\n", argv[i]);
                exit(1);
                break;
            }
        }
    }

    file_input  = argv[i++];
    if(i <= argc) file_output = argv[i++];
    if(i <= argc) file_offset = argv[i++];

    if(i <= argc) {
        printf("\nError: recheck your options, %s is not valid or some are missing\n", argv[i]);
        exit(1);
    }

    if(!g_in)       g_in       = calloc(INSZ,    1);
    if(!g_out)      g_out      = calloc(OUTSZ,   1);
    if(!g_filebuff) g_filebuff = calloc(FBUFFSZ, 1);
    if(!g_in || !g_out || !g_filebuff) std_err();

    if(check_is_dir(file_input)) {
        int     g_input_total_files;
        files_t *files  = NULL;
        int pathsz = strlen(file_input) + 2000; // PATHSZ
        u8  *filedir = malloc(pathsz + 1);
        strcpy(filedir, file_input);
        recursive_dir(filedir, pathsz);
        files = add_files(NULL, 0, &g_input_total_files);
        for(i = 0; i < g_input_total_files; i++) {
            if(i) printf("\n");
            file_input = files[i].name;
            g_folder_name = get_filename(file_input);
            offzip(file_input, file_output, file_offset, zipdo, &fdo);
        }
        FREE(filedir);
    } else {
        offzip(file_input, file_output, file_offset, zipdo, &fdo);
    }

    FCLOSE(&fdo);
    FCLOSE(&g_fdl);
    FREE(g_in);
    FREE(g_out);
    FREE(g_filebuff);
    return 0;
}



int offzip(u8 *file_input, u8 *file_output, u8 *file_offset, int zipdo, FILE **fdo) {
    FILE    *fdo_dummy = NULL; // totally useless, just for testing
    if(!fdo) fdo = &fdo_dummy;
    FILE    *fd     = NULL;
    u64     inlen,
            outlen;
    int     files;

        g_total_zsize   = 0;
        g_total_size    = 0;
        g_offset        = 0;
        g_filebuffoff   = 0;
        g_filebuffsz    = 0;
        g_last_offset   = 0;
        g_reimported    = 0;
        g_only_one_name = NULL;

    printf("- open input file:    %s\n", file_input);
    if(!strcmp(file_input, "-")) {
        fd = stdin;
    } else {
        fd = fopen(file_input, g_reimport ? "r+b" : "rb");
        if(!fd) std_err();
    }

    if(g_minzip > INSZ) g_minzip = INSZ;
    if(g_minzip < 1)    g_minzip = 1;

    if(g_try_output_dir) {
        g_try_output_dir = 0;
        if((zipdo == ZIPDODUMP) || (zipdo == ZIPDODUMP2)) {
            if(file_output && file_output[0]) {
                printf("- enter in directory: %s\n", file_output);
                if(chdir(file_output) < 0) {
                    if(g_only_one) {
                        g_only_one_name = file_output;
                    } else {
                        std_err();
                    }
                }
            }
        }
    }

    printf(
        "- zip data to check:  %d bytes\n"
        "- zip windowBits:     %d\n",
        g_minzip, g_zipwbits);

    g_offset = get_num(file_offset);  // do not skip, needed for buffseek
    printf("- seek offset:        0x%"PRIx"  (%"PRIu")\n", g_offset, g_offset);
    buffseek(fd, g_offset, SEEK_SET);

    memset(&z, 0, sizeof(z));
    if(inflateInit2(&z, g_zipwbits) != Z_OK) zlib_err(Z_INIT_ERROR);

    if(zipdo == ZIPDOFILE) {
        u8  *add_folder = g_folder_name;
        if(file_output && check_is_dir(file_output)) {
            if(file_output[0]) {
                chdir(file_output); // ... ignore the check
            }
            file_output = NULL;
            add_folder = NULL;
        }
        if(!file_output || !file_output[0]) {
            u8  *p, *ext;
            file_output = malloc(strlen(file_input) + (add_folder ? (strlen(add_folder)+1) : 0) + 64 + 1);
            p = strrchr(file_input, '\\');
            if(!p) p = strrchr(file_input, '/');
            if(!p) p = file_input;
            else   p++;
            ext = strrchr(p, '.');
            if(!ext) ext = p + strlen(p);
            file_output[0] = 0;
            if(add_folder) {
                sprintf(file_output + strlen(file_output), "%s", add_folder);
                make_dir(file_output);
                sprintf(file_output + strlen(file_output), "%c", PATHSLASH);
            }
            sprintf(file_output + strlen(file_output), "%.*s_%s", ext - p, p, "unpack");
            if(ext[0]) strcat(file_output, ext);
        }
        printf("- open output file:   %s\n", file_output);
        if(!*fdo) *fdo = save_file(file_output, 1);
        unzip(fd, fdo, &inlen, &outlen, zipdo, NULL);

        printf("\n"
            "- %"PRIu" bytes read (zipped)\n"
            "- %"PRIu" bytes unzipped\n",
            inlen, outlen);

    } else {
        printf("\n"
            "+------------+-----+----------------------------+----------------------+\n"
            "| hex_offset | ... | zip -> unzip size / offset | spaces before | info |\n"
            "+------------+-----+----------------------------+----------------------+\n");

        files = unzip_all(fd, fdo, zipdo);
        if(files) {
            //if(g_offset - g_last_offset) printf("  0x%08x spaces from the last compressed stream\n", g_offset - g_last_offset);
            printf("\n\n- %u valid compressed streams found\n", files);
            printf("- 0x");
            if(g_total_zsize >> 32LL) printf("%"PRIx, (g_total_zsize >> 32LL));
            printf("%"PRIx" -> 0x", g_total_zsize);
            if(g_total_size >> 32LL) printf("%"PRIx, (g_total_size >> 32LL));
            printf("%"PRIx" bytes covering the %"PRId"%% of the file\n", g_total_size,
                ((u64)((u64)g_total_zsize * (u64)100) / (u64)ftell(fd)));
            if(g_reimported) printf("- %u files reimported\n", g_reimported);
        } else {
            printf("\n\n- no valid full zip data found\n");
        }
    }

    if(!g_only_one) FCLOSE(fdo);
    FCLOSE(&fd);
    inflateEnd(&z);
    if(*fdo && (fdo == &fdo_dummy)) FCLOSE(fdo);
    return 0;
}



u8 *mystrrchrs(u8 *str, u8 *chrs) {
    u8      *p,
            *ret = NULL;

    if(str && chrs) {
        for(p = str + strlen(str) - 1; p >= str; p--) {
            if(strchr(chrs, *p)) return(p);
        }
    }
    return ret;
}



u8 *get_filename(u8 *fname) {
    u8      *p;

    if(fname) {
        p = mystrrchrs(fname, PATH_DELIMITERS);
        if(p) return(p + 1);
    }
    return(fname);
}



int make_dir(u8 *folder) {
    return mkdir(folder
#ifndef WIN32
    , 0755
#endif
    );
}



int check_is_dir(u8 *fname) {
    struct stat xstat;
    if(!fname || !fname[0]) return 1;
    if(!strcmp(fname, ".")) return 1;
    if(!strcmp(fname, "..")) return 1;
    if(stat(fname, &xstat) < 0) return 0;
    if(!S_ISDIR(xstat.st_mode)) return 0;
    return 1;
}



#define real_realloc    realloc
#define STD_ERR(X)      std_err()
int check_wildcards(u8 *fname, u8 **list) {
    return 0;
}



files_t *add_files(u8 *fname, u64 fsize, int *ret_files) {
    static int      filesi  = 0,
                    filesn  = 0;
    static files_t  *files  = NULL;
    files_t         *ret;

    if(ret_files) {
        *ret_files = filesi;
        files = real_realloc(files, sizeof(files_t) * (filesi + 1)); // not needed, but it's ok
        if(!files) STD_ERR(QUICKBMS_ERROR_MEMORY);
        files[filesi].name   = NULL;
        //files[filesi].offset = 0;
        files[filesi].size   = 0;
        ret    = files;
        filesi = 0;
        filesn = 0;
        files  = NULL;
        return ret;
    }

    if(!fname) return NULL;
    if(check_wildcards(fname, g_filter_in_files) < 0) return NULL;

    if(filesi >= filesn) {
        filesn += 1024;
        files = real_realloc(files, sizeof(files_t) * filesn);
        if(!files) STD_ERR(QUICKBMS_ERROR_MEMORY);
        memset(&files[filesi], 0, sizeof(files_t) * (filesn - filesi));
    }

    //mystrdup_simple(fname);
    files[filesi].name   = real_realloc(files[filesi].name, strlen(fname) + 1); // realloc in case of reusage
    if(!files[filesi].name) STD_ERR(QUICKBMS_ERROR_MEMORY);
    strcpy(files[filesi].name, fname);

    //files[filesi].offset = 0;
    files[filesi].size   = fsize;
    filesi++;
    return NULL;
}



#define recursive_dir_skip_path 0
//#define recursive_dir_skip_path 2
int recursive_dir(u8 *filedir, int filedirsz) {
    int     plen,
            namelen,
            ret     = -1;

    if(!filedir) return ret;
#ifdef WIN32
    static int      winnt = -1;
    OSVERSIONINFO   osver;
    WIN32_FIND_DATA wfd;
    HANDLE          hFind = INVALID_HANDLE_VALUE;

    if(winnt < 0) {
        osver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        GetVersionEx(&osver);
        if(osver.dwPlatformId >= VER_PLATFORM_WIN32_NT) {
            winnt = 1;
        } else {
            winnt = 0;
        }
    }

    plen = strlen(filedir);
    if((plen + 4) >= filedirsz) goto quit;
    strcpy(filedir + plen, "\\*.*");
    plen++;

    if(winnt) { // required to avoid problems with Vista and Windows7!
        hFind = FindFirstFileEx(filedir, FindExInfoStandard, &wfd, FindExSearchNameMatch, NULL, 0);
    } else {
        hFind = FindFirstFile(filedir, &wfd);
    }
    if(hFind == INVALID_HANDLE_VALUE) goto quit;
    do {
        if(!strcmp(wfd.cFileName, ".") || !strcmp(wfd.cFileName, "..")) continue;

        namelen = strlen(wfd.cFileName);
        if((plen + namelen) >= filedirsz) goto quit;
        //strcpy(filedir + plen, wfd.cFileName);
        memcpy(filedir + plen, wfd.cFileName, namelen);
        filedir[plen + namelen] = 0;

        if(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            recursive_dir(filedir, filedirsz);  // NO goto quit
        } else {
            add_files(filedir + recursive_dir_skip_path, ((u64)wfd.nFileSizeHigh << (u64)(sizeof(wfd.nFileSizeLow) * 8)) | (u64)wfd.nFileSizeLow, NULL);
        }
    } while(FindNextFile(hFind, &wfd));
    ret = 0;

quit:
    if(hFind != INVALID_HANDLE_VALUE) FindClose(hFind);
#else
    struct  stat    xstat;
    struct  dirent  **namelist;
    int     n,
            i;

    n = scandir(filedir, &namelist, NULL, NULL);
    if(n < 0) {
        if(stat(filedir, &xstat) < 0) {
            fprintf(stderr, "**** %s", filedir);
            STD_ERR(QUICKBMS_ERROR_FOLDER);
        }
        add_files(filedir + recursive_dir_skip_path, xstat.st_size, NULL);
        return 0;
    }

    plen = strlen(filedir);
    if((plen + 1) >= filedirsz) goto quit;
    strcpy(filedir + plen, "/");
    plen++;

    for(i = 0; i < n; i++) {
        if(!strcmp(namelist[i]->d_name, ".") || !strcmp(namelist[i]->d_name, "..")) continue;

        namelen = strlen(namelist[i]->d_name);
        if((plen + namelen) >= filedirsz) goto quit;
        //strcpy(filedir + plen, namelist[i]->d_name);
        memcpy(filedir + plen, namelist[i]->d_name, namelen);
        filedir[plen + namelen] = 0;

        if(stat(filedir, &xstat) < 0) {
            fprintf(stderr, "**** %s", filedir);
            STD_ERR(QUICKBMS_ERROR_FOLDER);
        }
        if(S_ISDIR(xstat.st_mode)) {
            recursive_dir(filedir, filedirsz);  // NO goto quit
        } else {
            add_files(filedir + recursive_dir_skip_path, xstat.st_size, NULL);
        }
        FREE(namelist[i]);
    }
    ret = 0;

quit:
    for(; i < n; i++) FREE(namelist[i]);
    FREE(namelist);
#endif
    filedir[plen - 1] = 0;
    return ret;
}



// these buffering functions are just specific for this usage

int buffread(FILE *fd, u8 *buff, int size) {
    int     len,
            rest,
            ret;

    if(size > FBUFFSZ) exit(1); // ???

    rest = g_filebuffsz - g_filebuffoff;

    ret = size;
    if(rest < size) {
        ret = size - rest;
        memmove(g_filebuff, g_filebuff + g_filebuffoff, rest);
        len = fread(g_filebuff + rest, 1, FBUFFSZ - rest, fd);
        g_filebuffoff = 0;
        g_filebuffsz  = rest + len;
        if(len < ret) {
            ret = rest + len;
        } else {
            ret = size;
        }
    }

    memcpy(buff, g_filebuff + g_filebuffoff, ret);
    return ret;
}



void buffseek(FILE *fd, u64 off, int mode) {
    if(fseek(fd, off, mode) < 0) std_err();
    g_filebuffoff = 0;
    g_filebuffsz  = 0;
    g_offset      = ftell(fd);
}



void buffinc(int increase) {
    g_filebuffoff += increase;
    g_offset      += increase;
}



int zip_search(FILE *fd) {
    int     len,
            zerr,
            ret;

    for(ret = - 1; (len = buffread(fd, g_in, g_minzip)) >= g_minzip; buffinc(1)) {
        z.next_in   = g_in;
        z.avail_in  = len;
        z.next_out  = g_out;
        z.avail_out = OUTSZ;

        inflateReset(&z);
        zerr = inflate(&z, Z_SYNC_FLUSH);

        if(zerr == Z_OK) {  // do not use Z_STREAM_END here! gives only troubles!!!
            if(!g_quiet) fprintf(stderr, "\r  0x%"PRIx"\r", g_offset);
            if(g_listfile) {
                if(!g_fdl) g_fdl = save_file(g_listfile, 0);
                fprintf(g_fdl, "0x%"PRIx"\n", g_offset);
            }
            ret = 0;
            break;
        }

        if(!g_quiet && !(g_offset & SHOWX)) fprintf(stderr, "\r  0x%"PRIx"\r", g_offset);
    }
    return ret;
}



int unzip_all(FILE *fd, FILE **fdo, int zipdo) {
    FILE    *fdo_dummy = NULL; // totally useless, just for testing
    if(!fdo) fdo = &fdo_dummy;
    u64     backup_offset,
            start_offset;
    u64     inlen,
            outlen,
            len;
    u32     crc;
    int     zipres,
            extracted;
    u8      filename[64]    = "",
            zlib_header[2],
            *tmp_buff       = NULL;

    extracted = 0;
    zipres    = -1;

    while(!zip_search(fd)) {
        printf("  0x%"PRIx" ", g_offset);
        start_offset = g_offset;

        switch(zipdo) {
            case ZIPDOSCAN2: {
                return 1;
                break;
            }
            case ZIPDOSCAN: {
                zipres = unzip(fd, fdo, &inlen, &outlen, zipdo, NULL);
                break;
            }
            case ZIPDODUMP:
            case ZIPDODUMP2: {
                filename[0] = 0;    // it means that the file will be not created
                if(!g_only_one || (g_only_one == 1)) {
                    sprintf(filename, "%"PRIx, g_offset);    // create the file
                    //sprintf(filename, "%"PRIx".dat", g_offset);
                    //*fdo = save_file(filename, 1);
                    if(g_only_one == 1) g_only_one = 2;
                }
                if(g_chunks) {
                    if(*fdo) filename[0] = 0;    // append to the current file
                    if((start_offset - g_last_offset) == g_chunks) {    // probably a non-compressed chunk?
                        if(*fdo) {
                            backup_offset = ftell(fd);
                            fseek(fd, start_offset, SEEK_SET);
                            tmp_buff = realloc(tmp_buff, g_chunks);
                            if(!tmp_buff) std_err();
                            len = fread(tmp_buff, 1, g_chunks, fd);
                            myfw(tmp_buff, len, *fdo);
                            fseek(fd, backup_offset, SEEK_SET);
                        }
                    }
                }
                zipres = unzip(fd, fdo, &inlen, &outlen, zipdo, filename);
                if(g_chunks) {
                    if(outlen < g_chunks) FCLOSE(fdo);   // probably the last chunk
                } else {
                    if(!g_only_one) FCLOSE(fdo);
                }
                if(g_reminval && (zipres < 0) && filename[0]) unlink(filename);
                break;
            }
            default: break;
        }

        if(!zipres) {
            if(g_hexview) {
                printf(" %"PRIx" -> %"PRIx"", inlen, outlen);
            } else {
                printf(" %"PRIu" -> %"PRIu"", inlen, outlen);
            }
            if(g_listfile) {
                if(!g_fdl) g_fdl = save_file(g_listfile, 0);
                if(g_hexview) {
                    fprintf(g_fdl, "0x%"PRIx" %"PRIx" %"PRIx"\n", g_offset, inlen, outlen);
                } else {
                    fprintf(g_fdl, "0x%"PRIx" %"PRIu" %"PRIu"\n", g_offset, inlen, outlen);
                }
            }
            extracted++;

            printf(" / 0x%"PRIx" _ ", g_offset);

            if(g_hexview) {
                printf("%"PRIx, (start_offset - g_last_offset));
            } else {
                printf("%"PRIu, (start_offset - g_last_offset));
            }
            g_last_offset = g_offset;   // g_offset points to start_offset + inlen, so it's ok

            if(g_zipwbits > 0) {
                backup_offset = ftell(fd);
                fseek(fd, start_offset, SEEK_SET);
                if(fread(zlib_header, 1, 2, fd) != 2) memset(zlib_header, 0, 2);
                fseek(fd, g_offset - 4, SEEK_SET);
                crc = (fgetc(fd) << 24) | (fgetc(fd) << 16) | (fgetc(fd) << 8) | fgetc(fd);
                printf(" %d:%d:%d:%d:%d:%08x",
                    (zlib_header[0] & 0xf),         // CM
                    (zlib_header[0] >> 4) & 0xf,    // CINFO
                    (zlib_header[1] & 0x1f),        // FCHECK
                    (zlib_header[1] >> 5) & 1,      // FDICT
                    (zlib_header[0] >> 6) & 3,      // FLEVEL
                    crc);                           // ADLER32
                fseek(fd, backup_offset, SEEK_SET);
            }

        } else {
            if(g_quiet > 0) printf(" error");
        }

        printf("\n");

        // after the first line of results
        if(!zipres) {
            if(g_vidsump > 0) {
                backup_offset = ftell(fd);
                tmp_buff = realloc(tmp_buff, g_vidsump);
                if(!tmp_buff) std_err();
                len = g_vidsump;
                if(start_offset < len) len = start_offset;
                fseek(fd, start_offset - len, SEEK_SET);
                len = fread(tmp_buff, 1, len, fd);
                offzip_show_dump(-1, tmp_buff, len, stdout);
                fseek(fd, g_offset, SEEK_SET);
                len = fread(tmp_buff, 1, g_vidsump, fd);
                offzip_show_dump(1, tmp_buff, len, stdout);
                fseek(fd, backup_offset, SEEK_SET);
                printf("\n");
            }
        }
    }

    if(*fdo && (fdo == &fdo_dummy)) FCLOSE(fdo);
    if(tmp_buff) FREE(tmp_buff);
    return extracted;
}



u8 *myzopfli(u8 *in, u64 insz, u64 *ret_outsz, int type) {
    size_t  outsz   = 0;
    u8      *out    = NULL;

    // the zopli options are a pain because the results (ratio and time) depends by the input file
    // the following are just the best I found on multiple tests
    ZopfliOptions   opt;
    memset(&opt, 0, sizeof(opt));
    ZopfliInitOptions(&opt);
         if(insz < (10 * 1024 * 1024))  opt.numiterations = 15; // this is
    else if(insz < (50 * 1024 * 1024))  opt.numiterations = 10; // just for
    else                                opt.numiterations = 5;  // speed
    opt.blocksplitting      = 1;
    opt.blocksplittinglast  = 0;
    opt.blocksplittingmax   = 0;
    ZopfliCompress(&opt, type, in, insz, &out, &outsz);

    if(ret_outsz) *ret_outsz = outsz;
    return out;
}



int unzip(FILE *fd, FILE **fdo, u64 *inlen, u64 *outlen, int zipdo, u8 *dumpname) {
    FILE    *fdo_dummy = NULL; // totally useless, just for testing
    if(!fdo) fdo = &fdo_dummy;
    u64     oldsz   = 0,
            oldoff;
    int     len;
    int     ret     = -1,
            zerr    = Z_OK,
            do_zip  = 0;
    u8      *freeme = NULL;

    if(dumpname && !dumpname[0]) dumpname = NULL;
    oldoff = g_offset;
    inflateReset(&z);
    if(g_dictionary) {
        inflateSetDictionary(&z, g_dictionary, g_dictionarysz);
        if(g_reimport) {
            fprintf(stderr, "\nError: dictionary not supported in reimport mode\n");
            exit(1);
        }
    }

    for(; (len = buffread(fd, g_in, INSZ)); buffinc(len)) {
        if(g_quiet >= 0) fputc('.', stderr);

        z.next_in   = g_in;
        z.avail_in  = len;
        do {
            z.next_out  = g_out;
            z.avail_out = OUTSZ;
            zerr = inflate(&z, Z_SYNC_FLUSH);

            switch(zipdo) {
                case ZIPDODUMP:
                case ZIPDOFILE:
                case ZIPDODUMP2: {
                    if(!dumpname) break;
                    if(g_only_one_name) {
                        dumpname = g_only_one_name;
                    } else {
                        if(g_folder_name) {
                            make_dir(g_folder_name);
                            freeme = realloc(freeme, strlen(g_folder_name) + 1 + strlen(dumpname) + 1 + 64 + 1);
                            sprintf(freeme, "%s%c%s", g_folder_name, PATHSLASH, dumpname);
                            dumpname = freeme;
                        }
                        sprintf(dumpname + strlen(dumpname), ".%s", sign_ext(g_out, z.total_out - oldsz));
                    }
                    if(!*fdo) *fdo = save_file(dumpname, 1);
                    if(g_reimport && *fdo) printf(" < reimporting %s\n", dumpname);
                    dumpname = NULL;
                }
                default: break;
            }

            switch(zipdo) {
                case ZIPDODUMP:
                case ZIPDOFILE: {
                    if(g_reimport) {
                        do_zip = 1;
                    } else {
                        myfw(g_out, z.total_out - oldsz, *fdo);
                        oldsz = z.total_out;
                    }
                    break;
                }
                case ZIPDODUMP2: {
                    if(g_reimport) {
                        do_zip = 0;
                    } else {
                        myfw(g_in, z.total_in - oldsz, *fdo);
                        oldsz = z.total_in;
                    }
                    break;
                }
                default: break;
            }

            if(zerr != Z_OK) {      // inflate() return value MUST be handled now
                if(zerr == Z_STREAM_END) {
                    ret = 0;
                } else {
                    if(!g_quiet) zlib_err(zerr);
                }
                break;
            }
            ret = 0;    // it's better to return 0 even if the z stream is incomplete... or not?
        } while(z.avail_in);

        if(zerr != Z_OK) break;     // Z_STREAM_END included, for avoiding "goto"
    }

    if(inlen)  *inlen  = z.total_in;
    if(outlen) *outlen = z.total_out;
    if(!ret) {
        if(g_reimport) {
            if(*fdo) {
                u64     insz, outsz;
                u8      *in, *out;

                in = fdloadx(NULL, &insz, *fdo);
                if(do_zip) {
                    out = myzopfli(in, insz, &outsz, (g_zipwbits > 0) ? ZOPFLI_FORMAT_ZLIB : ZOPFLI_FORMAT_DEFLATE);

                    if(outsz > z.total_in) {
                        fprintf(stderr, "\n"
                            "Error: the compressed data is bigger than the original one by 0x%"PRIx64" bytes\n"
                            "\n", (outsz - z.total_in));
                        exit(1);
                    }

                    fseek(fd, oldoff, SEEK_SET);
                    myfw(out, outsz, fd);
                    FREE(out);
                } else {
                    myfw(in, insz, fd);
                }
                FREE(in);

                g_reimported++;
            }
        }

        oldoff        += z.total_in;
        g_total_zsize += z.total_in;
        g_total_size  += z.total_out;
    } else {
        oldoff++;
    }
    buffseek(fd, oldoff, SEEK_SET);
    FREE(freeme);
    if(*fdo && (fdo == &fdo_dummy)) FCLOSE(fdo);
    return ret;
}



void offzip_show_dump(int before_after, unsigned char *data, unsigned int len, FILE *stream) {
    static const char   hex[] = "0123456789abcdef";
    int                 rem,
                        left;
    unsigned char       leftbuff[80],
                        buff[67],
                        chr,
                        *bytes,
                        *p,
                        *limit,
                        *glimit = data + len;

    left = 4;

    memset(buff + 2, ' ', 48);
    memset(leftbuff, ' ', sizeof(leftbuff));
    leftbuff[2] = (before_after < 0) ? '\\' : '/';

    while(data < glimit) {
        limit = data + 16;
        if(limit > glimit) {
            limit = glimit;
            memset(buff, ' ', 48);
        }

        p     = buff;
        bytes = p + 50;
        while(data < limit) {
            chr = *data;
            *p++ = hex[chr >> 4];
            *p++ = hex[chr & 15];
            p++;
            *bytes++ = ((chr < ' ') || (chr >= 0x7f)) ? '.' : chr;
            data++;
        }
        *bytes++ = '\n';

        for(rem = left; rem >= sizeof(leftbuff); rem -= sizeof(leftbuff)) {
            fwrite(leftbuff, sizeof(leftbuff), 1, stream);
        }
        if(rem > 0) fwrite(leftbuff, rem, 1, stream);
        fwrite(buff, bytes - buff, 1, stream);
    }
}



u64 readbase(u8 *data, u64 size, u64 *readn) {
    static const u8 table[256] =    // fast performances
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\xff\xff\xff\xff\xff\xff"
            "\xff\x0a\x0b\x0c\x0d\x0e\x0f\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\x0a\x0b\x0c\x0d\x0e\x0f\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"
            "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
    u64     num     = 0;
    int     sign;
    u8      c,
            *s,
            *hex_fix;

    s = data;
    if(!data || !size || !data[0]) {
        // do nothing (for readn)
    } else {
        // useful in some occasions, for example if the input is external!
        for(; *s; s++) {
            if(!strchr(" \t\r\n", *s)) break;
        }
        if(*s == '-') {
            sign = -1;
            s++;
        } else {
            sign = 0;
        }
        hex_fix = s;
        for(; *s; s++) {
            c = *s;
            //if((c == 'x') || (c == 'X') || (c == '$')) {  // auto base switching
            if(
                (((c == 'h') || (c == 'x') || (c == 'X')) && (s > hex_fix)) // 0x and 100h, NOT x123 or h123
             || (c == '$')                                                  // $1234 or 1234$
            ) {
                size = 16;
                continue;
            }
            c = table[c];
            if(c >= size) break;    // necessary to recognize the invalid chars based on the size
            num = (num * size) + c;
        }
        if(sign) num = -num;
    }
    if(readn) *readn = s - data;
    return(num);
}



u64 get_num(u8 *str) {
    return readbase(str, 10, NULL);
}



void zlib_err(int zerr) {
    switch(zerr) {
        case Z_DATA_ERROR: {
            fprintf(stderr, "\n"
                "- zlib Z_DATA_ERROR, the data in the file is not in zip format\n"
                "  or uses a different windowBits value (-z). Try to use -z %d\n",
                -g_zipwbits);
            break;
        }
        case Z_NEED_DICT: {
            fprintf(stderr, "\n"
                "- zlib Z_NEED_DICT, you need to set a dictionary (option not available)\n");
            break;
        }
        case Z_MEM_ERROR: {
            fprintf(stderr, "\n"
                "- zlib Z_MEM_ERROR, insufficient memory\n");
            break;
        }
        case Z_BUF_ERROR: {
            fprintf(stderr, "\n"
                "- zlib Z_BUF_ERROR, the output buffer for zlib decompression is not enough\n");
            break;
        }
        case Z_INIT_ERROR: {
            fprintf(stderr, "\nError: zlib initialization error (inflateInit2)\n");
            exit(1);
            break;
        }
        case Z_END_ERROR: {
            fprintf(stderr, "\nError: zlib free error (inflateEnd)\n");
            exit(1);
            break;
        }
        case Z_RESET_ERROR: {
            fprintf(stderr, "\nError: zlib reset error (inflateReset)\n");
            exit(1);
            break;
        }
        default: {
            fprintf(stderr, "\nError: zlib unknown error %d\n", zerr);
            exit(1);
            break;
        }
    }
}



u8 *fdloadx(u8 *fname, u64 *fsize, FILE *fd_in) {
    struct stat xstat;
    FILE    *fd;
    u64     size,
            tmp_off;
    u8      *buff;

    if(fd_in) {
        fd = fd_in;
        tmp_off = ftell(fd);
        fseek(fd, 0, SEEK_SET);
    } else {
        if(!fname) return NULL;
        //fprintf(stderr, "  %s\n", fname);
        fd = fopen(fname, "rb");
        if(!fd) {
            fprintf(stderr, "Error: the file %s doesn't exist\n", fname);
            exit(1);
        }
    }
    fstat(fileno(fd), &xstat);
    size = xstat.st_size;
    buff = calloc(size + 1, 1);
    if(!buff) std_err();
    size = fread(buff, 1, size, fd);
    buff[size] = 0;
    if(fd_in) {
        fseek(fd, tmp_off, SEEK_SET);
    } else {
        fclose(fd);
    }
    if(fsize) *fsize = size;
    return buff;
}



FILE *save_file(u8 *fname, int dump) {
    static int  all = 0;
    FILE    *fd;
    u8      ans[10];

    if(dump && g_reimport) {
        return fopen(fname, "rb");
    }

    if(!g_overwrite && !all) {
        fd = fopen(fname, "rb");
        if(fd) {
            fclose(fd);
            fprintf(stderr, "\n- the file \"%s\" already exists\n  do you want to overwrite it? (y/N/all)\n  ", fname);
            if(!fgets(ans, sizeof(ans), stdin)) exit(1);
            if(tolower(ans[0]) == 'a') {
                all = 1;
            } else if(tolower(ans[0]) != 'y') {
                exit(1);
            }
        }
    }
    fd = fopen(fname, "wb");
    if(!fd) std_err();
    return fd;
}



void myfw(u8 *buff, int size, FILE *fd) {
    if(!fd) {
        fprintf(stderr, "\nError: myfw fd is NULL, contact me.\n");
        exit(1);
    }
    if(size <= 0) return;
    if(fwrite(buff, 1, size, fd) != size) {
        fprintf(stderr, "\nError: problems during files writing, check permissions and disk space\n");
        exit(1);
    }
}



void FCLOSE(FILE **fd) {
    if(fd && *fd) {
        if((*fd != stdin) && (*fd != stdout)) {
            fclose(*fd);
        }
        *fd = NULL;
    }
}



void std_err(void) {
    perror("\nError");
    exit(1);
}


