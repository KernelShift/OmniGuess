#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__APPLE__)
  #include <Security/SecRandom.h>
#endif

#include "hashtable.h"
#include <secp256k1.h>

/* ---------------- constants ---------------- */
enum { KEY_HEX_LEN=66, KEY_STORE_BYTES=KEY_HEX_LEN+1, PRIV_LEN=32, COMP_PUB_LEN=33 };

/* ---------------- ANSI UI helpers ---------------- */
static void ui_hide_cursor(void){ fputs("\x1b[?25l", stdout); }
static void ui_show_cursor(void){ fputs("\x1b[?25h", stdout); }
static void ui_clear(void){ fputs("\x1b[2J\x1b[H", stdout); }
static void ui_home(void){ fputs("\x1b[H", stdout); }
static void ui_bold_on(void){ fputs("\x1b[1m", stdout); }
static void ui_bold_off(void){ fputs("\x1b[22m", stdout); }
static void ui_dim_on(void){ fputs("\x1b[2m", stdout); }
static void ui_dim_off(void){ fputs("\x1b[22m", stdout); }

/* ---------------- time & formatting ---------------- */
static inline long long ns_diff(const struct timespec *a, const struct timespec *b){
    return (b->tv_sec - a->tv_sec)*1000000000LL + (b->tv_nsec - a->tv_nsec);
}
static void fmt_commas_u64(uint64_t v, char *out, size_t outsz){
    if (v == 0){ snprintf(out, outsz, "0"); return; }
    char buf[32]; int n=0;
    while (v && n<(int)sizeof(buf)) { buf[n++] = (char)('0' + (v%10)); v/=10; }
    int first_group = n%3; if (first_group==0) first_group = 3;
    int i = n-1, outi=0, group = first_group;
    while (i >= 0 && outi < (int)outsz-1){
        out[outi++] = buf[i--];
        if (--group == 0 && i >= 0){
            if (outi < (int)outsz-1) out[outi++] = ',';
            group = 3;
        }
    }
    out[outi] = '\0';
}
static void fmt_compact_u64(uint64_t v, char *out, size_t outsz){
    struct { uint64_t t; const char *w; double d; } S[] = {
        {1000000000000ULL,"trillion",1e12}, {1000000000ULL,"billion",1e9},
        {1000000ULL,"million",1e6}, {1000ULL,"thousand",1e3}
    };
    for (size_t i=0;i<sizeof(S)/sizeof(S[0]);i++){
        if (v >= S[i].t){ snprintf(out,outsz,"%.1f %s",(double)v/S[i].d,S[i].w); return; }
    }
    snprintf(out,outsz,"%" PRIu64, v);
}
static void fmt_elapsed(double sec, char *out, size_t outsz){
    const double S=1,M=60*S,H=60*M,D=24*H,W=7*D,MO=30*D,YR=365*D;
    long yr=0,mo=0,wk=0,da=0,hr=0,mi=0; double s=sec;
    if (s>=YR){ yr=(long)(s/YR); s-=yr*YR; }
    if (s>=MO){ mo=(long)(s/MO); s-=mo*MO; }
    if (s>=W ){ wk=(long)(s/W ); s-=wk*W ; }
    if (s>=D ){ da=(long)(s/D ); s-=da*D ; }
    if (s>=H ){ hr=(long)(s/H ); s-=hr*H ; }
    if (s>=M ){ mi=(long)(s/M ); s-=mi*M ; }
    char buf[128]=""; int first=1;
    #define ADD(lbl,val) do{ if((val)>0){ snprintf(buf+strlen(buf),sizeof(buf)-strlen(buf),"%s%ld%s",first?"":" ",(long)(val),lbl); first=0; } }while(0)
    ADD("yr",yr); ADD("m",mo); ADD("w",wk); ADD("d",da); ADD("hr",hr); ADD("m",mi);
    #undef ADD
    if (first) snprintf(out,outsz,"%.2fs",s);
    else snprintf(out,outsz,"%s %.2fs",buf,s);
}
static void fmt_rate(double per_sec, char *out, size_t outsz){
    double per_min = per_sec*60.0, per_hr = per_min*60.0;
    uint64_t su = per_sec>0 ? (uint64_t)(per_sec+0.5) : 0;
    uint64_t mu = per_min>0 ? (uint64_t)(per_min+0.5) : 0;
    uint64_t hu = per_hr >0 ? (uint64_t)(per_hr +0.5) : 0;
    char sc[64], mc[64], hc[64], sx[64], mx[64], hx[64];
    fmt_commas_u64(su,sc,sizeof sc); fmt_commas_u64(mu,mc,sizeof mc); fmt_commas_u64(hu,hc,sizeof hc);
    fmt_compact_u64(su,sx,sizeof sx); fmt_compact_u64(mu,mx,sizeof mx); fmt_compact_u64(hu,hx,sizeof hx);
    snprintf(out,outsz,
        "Rate:\n"
        "  • %s keys/s   (%s)\n"
        "  • %s keys/min (%s)\n"
        "  • %s keys/hr  (%s)",
        sc,sx, mc,mx, hc,hx);
}

/* ---------------- text utils ---------------- */
static inline void rstrip(char *s){ size_t n=strlen(s); while(n && (s[n-1]=='\n'||s[n-1]=='\r'||isspace((unsigned char)s[n-1]))) s[--n]='\0'; }
static inline void lstrip_inplace(char *s){ size_t i=0; while(s[i]&&isspace((unsigned char)s[i])) i++; if(i) memmove(s,s+i,strlen(s+i)+1); }
static void uppercase_inplace(char *s){ for(char*p=s;*p;++p)*p=(char)toupper((unsigned char)*p); }
static void strip_utf8_bom_once(char *s){ unsigned char*u=(unsigned char*)s; if(u[0]==0xEF&&u[1]==0xBB&&u[2]==0xBF) memmove(s,s+3,strlen(s+3)+1); }
static int looks_like_len_hex(const char*s,size_t want){ if(strlen(s)!=want) return 0; for(size_t i=0;i<want;i++) if(!isxdigit((unsigned char)s[i])) return 0; return 1; }
static int looks_like_compressed_pubkey(const char*s){ if(!looks_like_len_hex(s,KEY_HEX_LEN)) return 0; return (s[0]=='0'&&(s[1]=='2'||s[1]=='3')); }
static void bytes_to_hex(const uint8_t*in,size_t n,char*out){ static const char H[]="0123456789ABCDEF"; for(size_t i=0;i<n;i++){ out[2*i]=H[(in[i]>>4)&0xF]; out[2*i+1]=H[in[i]&0xF]; } out[2*n]='\0'; }
static inline void make_keybuf_from_hex(const char*src,char out[KEY_STORE_BYTES]){ memcpy(out,src,KEY_HEX_LEN); out[KEY_HEX_LEN]='\0'; }

/* ---------------- RNG ---------------- */
#if defined(__APPLE__)
static inline void fast_random(uint8_t *dst, size_t n){
    if (SecRandomCopyBytes(kSecRandomDefault, n, dst) != errSecSuccess) {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd >= 0) { (void)read(fd, dst, n); close(fd); }
        else { for (size_t i=0;i<n;i++) dst[i] = (uint8_t)rand(); }
    }
}
#else
typedef struct { int fd; size_t pos,cap; uint8_t *buf; } UrandPool;
static int  urandpool_init(UrandPool*p,size_t cap){ p->fd=-1;p->pos=p->cap=0;p->buf=(uint8_t*)malloc(cap); if(!p->buf) return -1; p->cap=cap; p->fd=open("/dev/urandom",O_RDONLY); if(p->fd<0){ free(p->buf); p->buf=NULL; return -1;} p->pos=p->cap; return 0; }
static void urandpool_close(UrandPool*p){ if(p->fd>=0) close(p->fd); free(p->buf); p->buf=NULL; p->fd=-1; p->pos=p->cap=0; }
static inline void urandpool_get(UrandPool*p,uint8_t*dst,size_t n){ size_t rem=p->cap-p->pos; if(rem<n){ ssize_t rd=read(p->fd,p->buf,p->cap); if(rd!=(ssize_t)p->cap){ for(size_t i=0;i<n;i++) dst[i]=(uint8_t)rand(); return; } p->pos=0; } memcpy(dst,p->buf+p->pos,n); p->pos+=n; }
#endif

/* ---------------- options ---------------- */
typedef struct {
    const char *db_path;          /* default compressed_pubkeys.txt */
    int validate_db;              /* default 1 */
    double ui_interval;           /* seconds; default 0.25 */
    int quiet;                    /* 0 = full-screen UI */
    int threads;                  /* manual thread count; default 1 */
} Opts;

static void opts_init(Opts*o){ o->db_path="compressed_pubkeys.txt"; o->validate_db=1; o->ui_interval=0.25; o->quiet=0; o->threads=1; }
static void usage(const char*prog){
    fprintf(stderr,"Usage: %s [--db=PATH] [--no-validate-db] [--ui-interval=SEC] [--threads=N] [--quiet]\n", prog);
}

/* ---------------- globals ---------------- */
static volatile sig_atomic_t g_int = 0;
static void on_sigint(int sig){ (void)sig; g_int = 1; }

/* -------- worker state (per thread) -------- */
typedef struct {
    const HashTable *ht;
    atomic_bool *stop;
    atomic_uint_least64_t *counts;
    int idx;
    pthread_mutex_t *found_mu;
    char *found_priv_hex;
    char *found_pub_hex;
#if !defined(__APPLE__)
    UrandPool pool; int pool_ok;
#endif
} WorkerCtx;

static void* worker_fn(void *arg){
    WorkerCtx *ctx=(WorkerCtx*)arg;
    secp256k1_context *sctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN|SECP256K1_CONTEXT_VERIFY);
    if(!sctx) pthread_exit(NULL);
#if !defined(__APPLE__)
    ctx->pool_ok = (urandpool_init(&ctx->pool, 1<<20) == 0);
#endif

    char priv_hex[PRIV_LEN*2+1], pub_hex[KEY_HEX_LEN+1];

    while(!atomic_load_explicit(ctx->stop, memory_order_relaxed)){
        uint8_t seckey[PRIV_LEN];
#if defined(__APPLE__)
        fast_random(seckey,sizeof seckey);
#else
        if(ctx->pool_ok) urandpool_get(&ctx->pool,seckey,sizeof seckey);
        else for (int i=0;i<PRIV_LEN;i++) seckey[i]=(uint8_t)rand();
#endif
        if (!secp256k1_ec_seckey_verify(sctx,seckey)) continue;

        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(sctx,&pubkey,seckey)) continue;

        unsigned char pub33[COMP_PUB_LEN]; size_t L=COMP_PUB_LEN;
        secp256k1_ec_pubkey_serialize(sctx,pub33,&L,&pubkey,SECP256K1_EC_COMPRESSED);

        bytes_to_hex(seckey,PRIV_LEN,priv_hex);
        bytes_to_hex(pub33,COMP_PUB_LEN,pub_hex);

        char kbuf[KEY_STORE_BYTES]; make_keybuf_from_hex(pub_hex,kbuf);
        if (ht_contains((HashTable*)ctx->ht,kbuf)){
            if (!atomic_exchange(ctx->stop, true)){
                pthread_mutex_lock(ctx->found_mu);
                strcpy(ctx->found_priv_hex,priv_hex);
                strcpy(ctx->found_pub_hex ,pub_hex);
                pthread_mutex_unlock(ctx->found_mu);
            }
            break;
        }
        atomic_fetch_add_explicit(&ctx->counts[ctx->idx],1,memory_order_relaxed);
    }

#if !defined(__APPLE__)
    if(ctx->pool_ok) urandpool_close(&ctx->pool);
#endif
    secp256k1_context_destroy(sctx);
    return NULL;
}

/* ---------------- main ---------------- */
int main(int argc, char **argv){
    Opts opt; opts_init(&opt);
    for (int i=1;i<argc;i++){
        if (strncmp(argv[i],"--db=",5)==0){ opt.db_path=argv[i]+5; continue; }
        if (strcmp(argv[i],"--no-validate-db")==0){ opt.validate_db=0; continue; }
        if (strncmp(argv[i],"--ui-interval=",14)==0){ opt.ui_interval=atof(argv[i]+14); if(opt.ui_interval<=0) opt.ui_interval=0.25; continue; }
        if (strncmp(argv[i],"--threads=",10)==0){ opt.threads=atoi(argv[i]+10); if(opt.threads<1) opt.threads=1; continue; }
        if (strcmp(argv[i],"--quiet")==0){ opt.quiet=1; continue; }
        if (!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){ usage(argv[0]); return 0; }
        fprintf(stderr,"Unknown option: %s\n", argv[i]); usage(argv[0]); return 2;
    }

    signal(SIGINT,on_sigint);

    struct timespec t0; clock_gettime(CLOCK_MONOTONIC,&t0);

    /* Load DB -> hashtable (uppercase + BOM strip) */
    static char db_buf[1<<20];
    FILE *db=fopen(opt.db_path,"r");
    if(!db){ fprintf(stderr,"Failed to open DB '%s': %s\n", opt.db_path, strerror(errno)); return 2; }
    setvbuf(db,db_buf,_IOFBF,sizeof db_buf);

    HashTable ht;
    if (ht_setup(&ht,KEY_STORE_BYTES,sizeof(uint8_t),1024)!=0){
        fprintf(stderr,"ht_setup failed\n"); fclose(db); return 2;
    }

    size_t linecap=0; char *line=NULL;
    size_t valid_count=0, loaded=0, skipped=0;
    long pos=ftell(db);
    while(getline(&line,&linecap,db)!=-1){
        rstrip(line); lstrip_inplace(line);
        if(*line=='\0') continue;
        if(!opt.validate_db || looks_like_compressed_pubkey(line)) valid_count++;
    }
    free(line); line=NULL; linecap=0;
    fseek(db,pos,SEEK_SET);

    size_t reserve=1; while(reserve < valid_count*2) reserve<<=1;
    ht_reserve(&ht,(uint32_t)reserve);

    int bom_checked=0;
    while(getline(&line,&linecap,db)!=-1){
        rstrip(line); lstrip_inplace(line);
        if(*line=='\0') continue;
        if(!bom_checked){ strip_utf8_bom_once(line); bom_checked=1; }
        if(opt.validate_db && !looks_like_compressed_pubkey(line)){ skipped++; continue; }
        uppercase_inplace(line);
        char kbuf[KEY_STORE_BYTES]; make_keybuf_from_hex(line,kbuf);
        uint8_t one=1;
        if(ht_insert(&ht,kbuf,&one)==0) loaded++; else skipped++;
    }
    free(line); line=NULL; linecap=0; fclose(db);

    struct timespec t1; clock_gettime(CLOCK_MONOTONIC,&t1);
    double load_ms = ns_diff(&t0,&t1)/1e6;

    int nthreads = opt.threads; /* manual: no auto-detect */

    if(!opt.quiet){ ui_clear(); ui_hide_cursor(); atexit(ui_show_cursor); }
    if(!opt.quiet){
        ui_home();
        ui_bold_on(); printf("SECP256K1 Hunter — multi-threaded (until first match)\n"); ui_bold_off();
        printf("\nDatabase:\n  • File: %s\n  • Loaded keys: %zu  (reserve=%zu)  load: %.3f ms\n", opt.db_path, loaded, reserve, load_ms);
        printf("Workers:\n  • Threads: %d\n\n", nthreads);
        printf("Status:\n  • State: RUNNING\n");
        printf("  • Elapsed: --\n  • Checked: --\n  • Rate:\n    • -- keys/s   (--)\n    • -- keys/min (--)\n    • -- keys/hr  (--)\n\n");
        ui_dim_on(); printf("Press Ctrl+C to stop (no match).\n"); ui_dim_off(); fflush(stdout);
    }

    /* thread state */
    atomic_bool stop=false;
    atomic_uint_least64_t *counts = calloc(nthreads, sizeof(*counts));
    if(!counts){ fprintf(stderr,"OOM\n"); ht_destroy(&ht); return 2; }

    pthread_mutex_t found_mu; pthread_mutex_init(&found_mu,NULL);
    char found_priv_hex[PRIV_LEN*2+1]={0};
    char found_pub_hex [KEY_HEX_LEN+1]={0};

    pthread_t *tids = malloc(nthreads*sizeof(*tids));
    WorkerCtx *wctx  = malloc(nthreads*sizeof(*wctx));
    if(!tids || !wctx){ fprintf(stderr,"OOM\n"); free(counts); ht_destroy(&ht); return 2; }

    for(int i=0;i<nthreads;i++){
        wctx[i].ht=&ht; wctx[i].stop=&stop; wctx[i].counts=counts; wctx[i].idx=i;
        wctx[i].found_mu=&found_mu; wctx[i].found_priv_hex=found_priv_hex; wctx[i].found_pub_hex=found_pub_hex;
        pthread_create(&tids[i],NULL,worker_fn,&wctx[i]);
    }

    struct timespec last_ui=t1;
    while(!atomic_load(&stop)){
        if(g_int){ atomic_store(&stop,true); break; }
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        double since = ns_diff(&last_ui,&now)/1e9;
        if(!opt.quiet && since >= opt.ui_interval){
            uint64_t total=0; for(int i=0;i<nthreads;i++) total += atomic_load_explicit(&counts[i], memory_order_relaxed);
            double elapsed = ns_diff(&t0,&now)/1e9;
            char elapsed_s[128]; fmt_elapsed(elapsed,elapsed_s,sizeof elapsed_s);
            char checked_c[64], checked_x[64];
            fmt_commas_u64(total,checked_c,sizeof checked_c);
            fmt_compact_u64(total,checked_x,sizeof checked_x);
            double r = elapsed>0 ? (double)total/elapsed : 0.0;
            char rb[256]; fmt_rate(r,rb,sizeof rb);

            ui_home();
            ui_bold_on(); printf("SECP256K1 Hunter — multi-threaded (until first match)\n"); ui_bold_off();
            printf("\nDatabase:\n  • File: %s\n  • Loaded keys: %zu  (reserve=%zu)  load: %.3f ms\n", opt.db_path, loaded, reserve, load_ms);
            printf("Workers:\n  • Threads: %d\n\n", nthreads);
            printf("Status:\n  • State: RUNNING\n");
            printf("  • Elapsed: %s\n", elapsed_s);
            printf("  • Checked: %s  (%s)\n", checked_c, checked_x);
            printf("  • %s\n\n", rb);
            ui_dim_on(); printf("Press Ctrl+C to stop (no match).\n"); ui_dim_off();
            fflush(stdout);
            last_ui=now;
        }
        struct timespec ts={0,10*1000*1000}; nanosleep(&ts,NULL); /* 10ms */
    }

    for(int i=0;i<nthreads;i++) pthread_join(tids[i],NULL);

    if (found_pub_hex[0]){
        FILE *mf=fopen("matches.txt","a");
        if(mf){ fprintf(mf,"%s,%s\n",found_priv_hex,found_pub_hex); fclose(mf); }
        if(!opt.quiet){ ui_home(); ui_clear(); ui_show_cursor(); }

        struct timespec t2; clock_gettime(CLOCK_MONOTONIC,&t2);
        double elapsed = ns_diff(&t0,&t2)/1e9;
        uint64_t total=0; for(int i=0;i<nthreads;i++) total+=atomic_load(&counts[i]);
        char elapsed_s[128]; fmt_elapsed(elapsed,elapsed_s,sizeof elapsed_s);
        char checked_c[64], checked_x[64]; fmt_commas_u64(total,checked_c,sizeof checked_c); fmt_compact_u64(total,checked_x,sizeof checked_x);
        double r= elapsed>0 ? (double)total/elapsed : 0.0; char rb[256]; fmt_rate(r,rb,sizeof rb);

        ui_bold_on(); printf("FOUND MATCH!\n"); ui_bold_off();
        printf("\nPrivate key: %s\nPub (cmp):  %s\n\n", found_priv_hex, found_pub_hex);
        printf("Total checked: %s  (%s)\n", checked_c, checked_x);
        printf("Elapsed: %s\n%s\n\nSaved to matches.txt\n", elapsed_s, rb);
    } else {
        if(!opt.quiet){ ui_home(); ui_clear(); ui_show_cursor(); }
        printf("Stopped. No match.\n");
    }

    pthread_mutex_destroy(&found_mu);
    free(wctx); free(tids); free(counts);
    ht_destroy(&ht);
    return found_pub_hex[0] ? 0 : 1;
}