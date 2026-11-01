static uint32_t call_guest(void *opaque,uint32_t target,uint32_t a,uint32_t c,uint32_t d,const uint32_t *args,size_t count)
{
    (void)opaque;
    recomp_func_t fn=recomp_lookup_manual(target);if(!fn)fn=recomp_lookup(target);
    if(!fn||!RECOMP_ICALL_IS_CODE(target)||count>8||!memory_ok(g_esp-(uint32_t)(count*4+4),count*4+4)){
        menu.fault=1;return 0;
    }
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp,g_fs_base,g_mxcsr};
    int df=g_df,top=g_fp_top,cmp=g_fp_cmp;uint16_t control=g_fp_control_word,cc=g_fp_cc;
    double fp[8];memcpy(fp,g_fp_stack,sizeof fp);
    RecompXmm sse[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx mmx[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    for(size_t i=count;i>0;--i){g_esp-=4;MEM32(g_esp)=args[i-1];}
    g_esp-=4;MEM32(g_esp)=0x124374;g_eax=a;g_ecx=c;g_edx=d;
    fn();uint32_t result=g_eax;
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];g_ebx=regs[4];g_esi=regs[5];g_edi=regs[6];
    g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_mxcsr=regs[10];
    g_df=df;g_fp_top=top;g_fp_cmp=cmp;g_fp_control_word=control;g_fp_cc=cc;memcpy(g_fp_stack,fp,sizeof fp);
    g_xmm0=sse[0];g_xmm1=sse[1];g_xmm2=sse[2];g_xmm3=sse[3];g_xmm4=sse[4];g_xmm5=sse[5];g_xmm6=sse[6];g_xmm7=sse[7];
    g_mm0=mmx[0];g_mm1=mmx[1];g_mm2=mmx[2];g_mm3=mmx[3];g_mm4=mmx[4];g_mm5=mmx[5];g_mm6=mmx[6];g_mm7=mmx[7];
    return result;
}
