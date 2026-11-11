/* Exercise the production worker with synthetic pixels only. */
#include "../../ports/macos/overrides/texture_dump.h"
#include "test_temp.h"
#include "../../ports/macos/overrides/texture_pack.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char root[]="/tmp/black-texture-dump.XXXXXX", path[1024];
    if(!black_test_temp_dir(root) || setenv("BLACK_TEXTURE_DUMP_DIR",root,1)) return 1;
    unsigned char pixels[64], palette[1024]={0}, dxt[32]={0};
    for(int i=0;i<64;i++)pixels[i]=(unsigned char)i;
    for(int i=0;i<256;i++){palette[i*4]=(unsigned char)i;palette[i*4+1]=(unsigned char)(2*i);palette[i*4+2]=(unsigned char)(255-i);palette[i*4+3]=255;}
    black_TextureDump(11,8,8,pixels,sizeof pixels,palette,sizeof palette);
    black_TextureDump(11,8,8,pixels,sizeof pixels,palette,sizeof palette);
    black_TextureDump(11,8,8,pixels,sizeof pixels,palette,256);
    black_TextureDump(11,8,8,pixels,sizeof pixels,palette,128); /* index 63 exceeds palette */
    palette[2]=10;
    black_TextureDump(11,8,8,pixels,sizeof pixels,palette,sizeof palette);
    black_TextureDump(12,8,8,dxt,sizeof dxt,NULL,0);
    int n=0, changed=0, original=0, compressed=0;
    for(int attempt=0;attempt<200 && n<4;attempt++){
        usleep(10000);DIR *d=opendir(root);if(!d)return 2;n=changed=original=compressed=0;
        struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;
            snprintf(path,sizeof path,"%s/%s",root,e->d_name);BlackTextureImage img;
            if(!black_TextureLoadFile(path,&img))continue;
            n++;if(img.format==BLACK_TEX_BC1 && img.level_size[0]==32 && !memcmp(img.level[0],dxt,32))compressed++;
            if(img.format==BLACK_TEX_RGBA8 && img.width==8 && img.height==8){const unsigned char *p=img.level[0];
                if(p[0]==255 && p[1]==0 && p[2]==0 && p[3]==255)original++;
                if(p[0]==10 && p[1]==0 && p[2]==0 && p[3]==255)changed++;
                if(p[4]!=254 || p[5]!=2 || p[6]!=1 || p[32]!=253 || p[33]!=4 || p[34]!=2)return 3;
            }
            black_TextureImageFree(&img);
        }closedir(d);
    }
    DIR *d=opendir(root);if(!d)return 4;struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;snprintf(path,sizeof path,"%s/%s",root,e->d_name);unlink(path);}closedir(d);rmdir(root);
    if(n!=4||original!=2||changed!=1||compressed!=1){fprintf(stderr,"dump files=%d original=%d changed=%d DDS=%d\n",n,original,changed,compressed);return 5;}
    puts("texture dump: original DDS, I8 palette variants, swizzle and dedup PASS");return 0;
}
