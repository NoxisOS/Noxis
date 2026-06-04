/**
 * @file    src/lib/noxlib/noxlib.h
 * @brief   Minimal 64-bit userland C runtime (syscall wrappers + helpers).
 */
#ifndef NOXLIB_H
#define NOXLIB_H

typedef unsigned long  size_t;
typedef long           ssize_t;

#define SYS_EXIT      0
#define SYS_WRITE     1
#define SYS_READ      2
#define SYS_FORK      3
#define SYS_EXEC      4
#define SYS_GETPID    5
#define SYS_WAITPID   6
#define SYS_OPEN      7
#define SYS_CLOSE     8
#define SYS_LSEEK     9
#define SYS_READDIR   10
#define SYS_DUP       11
#define SYS_DUP2      12
#define SYS_PIPE      13
#define SYS_KILL      14
#define SYS_SIGNAL    15
#define SYS_PROCINFO  16
#define SYS_SETFG     17
#define SYS_CHDIR     18
#define SYS_GETCWD    19
#define SYS_GETDENTS  20
#define SYS_BRK       21
#define SYS_MKDIR     22
#define SYS_UNLINK    23
#define SYS_STAT      24
#define SYS_RENAME    25
#define SYS_TCGETATTR 26
#define SYS_TCSETATTR 27
#define SYS_SLEEP       28
#define SYS_UPTIME      29
#define SYS_GETENV      30
#define SYS_SETENV      31
#define SYS_UNSETENV    32
#define SYS_GETENV_AT   33

#define SIGINT   2
#define SIGKILL  9
#define SIGUSR1  10
#define SIGTERM  15

#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_CREAT   0x40
#define O_TRUNC   0x200

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── Raw syscall (SysV ABI: num=rax, args=rdi/rsi/rdx) ────────────────── */
static inline long _syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ __volatile__("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory");
    return ret;
}

/* ── Basic I/O ──────────────────────────────────────────────────────────── */
static inline ssize_t write(int fd, const void* buf, size_t len) {
    return _syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}
static inline ssize_t read(int fd, void* buf, size_t len) {
    return _syscall3(SYS_READ, fd, (long)buf, (long)len);
}
static inline void exit(int code) { _syscall3(SYS_EXIT, code, 0, 0); for(;;){} }

/* ── Process ────────────────────────────────────────────────────────────── */
static inline long fork(void)  { return _syscall3(SYS_FORK, 0, 0, 0); }
static inline long execv(const char* p, char* const argv[]) {
    return _syscall3(SYS_EXEC, (long)p, (long)argv, 0);
}
static inline long getpid(void) { return _syscall3(SYS_GETPID, 0, 0, 0); }
static inline long waitpid(long pid, int* st) {
    return _syscall3(SYS_WAITPID, pid, (long)st, 0);
}

/* ── File descriptors ───────────────────────────────────────────────────── */
static inline long open(const char* path, int flags) {
    return _syscall3(SYS_OPEN, (long)path, flags, 0);
}
static inline long close(int fd)       { return _syscall3(SYS_CLOSE, fd, 0, 0); }
static inline long lseek(int fd, long off, int whence) {
    return _syscall3(SYS_LSEEK, fd, off, whence);
}
static inline long dup(int fd)         { return _syscall3(SYS_DUP, fd, 0, 0); }
static inline long dup2(int o, int n)  { return _syscall3(SYS_DUP2, o, n, 0); }
static inline long pipe(int fds[2])    { return _syscall3(SYS_PIPE, (long)fds, 0, 0); }
static inline long readdir(long idx, char* buf) {
    return _syscall3(SYS_READDIR, idx, (long)buf, 0);
}

/* ── Signals / foreground ───────────────────────────────────────────────── */
static inline long kill(long pid, int sig) { return _syscall3(SYS_KILL, pid, sig, 0); }
static inline long signal(int sig, void (*h)(int)) {
    return _syscall3(SYS_SIGNAL, sig, (long)h, 0);
}
static inline void setfg(long pid) { _syscall3(SYS_SETFG, pid, 0, 0); }

/* ── Process info ───────────────────────────────────────────────────────── */
typedef struct { long pid; long state; char name[32]; } procinfo_t;
static inline long procinfo(long idx, procinfo_t* pi) {
    return _syscall3(SYS_PROCINFO, idx, (long)pi, 0);
}

/* ── Directory / filesystem ─────────────────────────────────────────────── */
static inline long chdir(const char* path) {
    return _syscall3(SYS_CHDIR, (long)path, 0, 0);
}
static inline long getcwd(char* buf, long size) {
    return _syscall3(SYS_GETCWD, (long)buf, size, 0);
}

typedef struct {
    unsigned int   inode;
    unsigned short rec_len;
    unsigned char  name_len;
    unsigned char  type;    /* 1=file, 2=dir */
    char           name[24];
} dirent_t;

static inline long getdents(const char* path, void* buf, long len) {
    return _syscall3(SYS_GETDENTS, (long)path, (long)buf, len);
}

static inline long mkdir(const char* path)  { return _syscall3(SYS_MKDIR,  (long)path, 0, 0); }
static inline long unlink(const char* path) { return _syscall3(SYS_UNLINK, (long)path, 0, 0); }

typedef struct {
    unsigned int   ino;
    unsigned short mode;
    unsigned short _pad;
    unsigned int   size;
    unsigned int   _pad2;
} stat_t;
#define S_ISREG(m)  (((m) & 0x8000u) != 0)
#define S_ISDIR(m)  (((m) & 0x4000u) != 0)

static inline long stat(const char* path, stat_t* buf) {
    return _syscall3(SYS_STAT, (long)path, (long)buf, 0);
}
static inline long rename(const char* old, const char* newp) {
    return _syscall3(SYS_RENAME, (long)old, (long)newp, 0);
}

/* ── String helpers ─────────────────────────────────────────────────────── */
static inline size_t strlen(const char* s) { size_t n=0; while(s[n])n++; return n; }
static inline int atoi(const char* s) {
    int v=0,neg=0; if(*s=='-'){neg=1;s++;} else if(*s=='+')s++;
    while(*s>='0'&&*s<='9') v=v*10+(*s++-'0'); return neg?-v:v;
}
static inline const char* strstr(const char* h, const char* n) {
    if(!*n)return h;
    for(;*h;h++){const char*hh=h,*nn=n;while(*nn&&*hh==*nn){hh++;nn++;}if(!*nn)return h;}
    return(const char*)0;
}
static inline int strcmp(const char* a, const char* b) {
    while (*a && *a==*b){a++;b++;} return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
static inline int strncmp(const char* a, const char* b, size_t n) {
    for(size_t i=0;i<n;i++){if(a[i]!=b[i])return(int)(unsigned char)a[i]-(int)(unsigned char)b[i];if(!a[i])return 0;}
    return 0;
}
static inline void puts(const char* s) { write(1, s, strlen(s)); }
static inline void puti(long v) {
    char buf[24]; int i=sizeof(buf);
    unsigned long u=(v<0)?(unsigned long)(-v):(unsigned long)v;
    buf[--i]=0; do{buf[--i]=(char)('0'+u%10);u/=10;}while(u);
    if(v<0)buf[--i]='-'; write(1,&buf[i],strlen(&buf[i]));
}

/* ── Memory helpers ─────────────────────────────────────────────────────── */
static inline void* memcpy(void* d, const void* s, size_t n) {
    char* dd=(char*)d; const char* ss=(const char*)s;
    for(size_t i=0;i<n;i++)dd[i]=ss[i]; return d;
}
static inline void* memset(void* d, int c, size_t n) {
    char* dd=(char*)d; for(size_t i=0;i<n;i++)dd[i]=(char)c; return d;
}
static inline int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* p=(const unsigned char*)a, *q=(const unsigned char*)b;
    for(size_t i=0;i<n;i++) if(p[i]!=q[i]) return(int)p[i]-(int)q[i];
    return 0;
}

/* ── Sleep ──────────────────────────────────────────────────────────────── */
static inline void sleep_ms(unsigned long ms) { _syscall3(SYS_SLEEP,(long)ms,0,0); }
static inline unsigned int sleep(unsigned int sec) { sleep_ms((unsigned long)sec*1000UL); return 0; }
static inline unsigned long uptime_ms(void) {
    return (unsigned long)_syscall3(SYS_UPTIME,0,0,0);
}

/* ── Environment variables ───────────────────────────────────────────────── */
/* getenv(key): return pointer to static buffer, or NULL. */
static inline const char* getenv(const char* key) {
    static char _ge[160];
    if (_syscall3(SYS_GETENV,(long)key,(long)_ge,sizeof(_ge)) >= 0) return _ge;
    return (const char*)0;
}
/* setenv: overwrite=0 keeps existing value. */
static inline long setenv(const char* key, const char* val, int overwrite) {
    if (!overwrite) { char _t[8]; if(_syscall3(SYS_GETENV,(long)key,(long)_t,8)>=0)return 0; }
    return _syscall3(SYS_SETENV,(long)key,(long)val,0);
}
static inline long unsetenv(const char* key) {
    return _syscall3(SYS_UNSETENV,(long)key,0,0);
}
/* putenv("KEY=VAL"): parse and call setenv. */
static inline long putenv(const char* s) {
    char key[32]; int ki=0;
    while(s[ki]&&s[ki]!='='&&ki<31){key[ki]=s[ki];ki++;} key[ki]=0;
    return _syscall3(SYS_SETENV,(long)key,(long)(s[ki]=='='?s+ki+1:""),(long)0);
}
/* Iterate over all env vars: getenv_at(idx, buf, bufsz) returns "KEY=VAL" or -1. */
static inline long getenv_at(long idx, char* buf, long bufsz) {
    return _syscall3(SYS_GETENV_AT,idx,(long)buf,bufsz);
}

/* ── printf / sprintf / fprintf ─────────────────────────────────────────── */
typedef __builtin_va_list _va_list;
#define _va_start(ap,l) __builtin_va_start(ap,l)
#define _va_arg(ap,T)   __builtin_va_arg(ap,T)
#define _va_end(ap)     __builtin_va_end(ap)

static inline int vsnprintf(char* b, size_t sz, const char* fmt, _va_list ap) {
    if(!sz) return 0; size_t p=0;
#define _PC(c) do{if(p<sz-1)b[p++]=(char)(c);}while(0)
    while(*fmt) {
        if(*fmt!='%'){_PC(*fmt++);continue;} fmt++;
        int lft=0,zer=0,wid=0,lng=0;
        for(;;){if(*fmt=='-'){lft=1;fmt++;}else if(*fmt=='0'){zer=1;fmt++;}else break;}
        while(*fmt>='0'&&*fmt<='9') wid=wid*10+(*fmt++-'0');
        while(*fmt=='l'){lng++;fmt++;}
        char sp=*fmt++; if(!sp)break;
        char tmp[32]; int tl=0;
        if(sp=='c'){tmp[tl++]=(char)_va_arg(ap,int);}
        else if(sp=='s'){
            const char* s=_va_arg(ap,const char*); if(!s)s="(null)";
            int sl=0; const char* q=s; while(*q++)sl++;
            if(!lft&&wid>sl)for(int i=0;i<wid-sl&&p<sz-1;i++)b[p++]=' ';
            while(*s&&p<sz-1)b[p++]=(char)*s++;
            if(lft&&wid>sl)for(int i=0;i<wid-sl&&p<sz-1;i++)b[p++]=' ';
            continue;
        }
        else if(sp=='d'||sp=='i'){
            long long v=(lng>=2)?_va_arg(ap,long long):(lng?_va_arg(ap,long):(long long)_va_arg(ap,int));
            unsigned long long u=(v<0)?(unsigned long long)(-v):(unsigned long long)v;
            if(v<0)tmp[tl++]='-';
            char tb[24];int ti=24;if(!u)tb[--ti]='0';else while(u){tb[--ti]='0'+(u%10);u/=10;}
            while(ti<24)tmp[tl++]=tb[ti++];
        }
        else if(sp=='u'){
            unsigned long long u=(lng>=2)?_va_arg(ap,unsigned long long):(lng?_va_arg(ap,unsigned long):(unsigned long long)_va_arg(ap,unsigned int));
            char tb[24];int ti=24;if(!u)tb[--ti]='0';else while(u){tb[--ti]='0'+(u%10);u/=10;}
            while(ti<24)tmp[tl++]=tb[ti++];
        }
        else if(sp=='x'||sp=='X'){
            unsigned long long u=(lng>=2)?_va_arg(ap,unsigned long long):(lng?_va_arg(ap,unsigned long):(unsigned long long)_va_arg(ap,unsigned int));
            const char* hx=(sp=='x')?"0123456789abcdef":"0123456789ABCDEF";
            char tb[24];int ti=24;if(!u)tb[--ti]='0';else while(u){tb[--ti]=hx[u&15];u>>=4;}
            while(ti<24)tmp[tl++]=tb[ti++];
        }
        else if(sp=='p'){
            unsigned long long u=(unsigned long long)(unsigned long)_va_arg(ap,void*);
            tmp[tl++]='0';tmp[tl++]='x';
            const char* hx="0123456789abcdef";char tb[24];int ti=24;
            if(!u)tb[--ti]='0';else while(u){tb[--ti]=hx[u&15];u>>=4;}
            while(ti<24)tmp[tl++]=tb[ti++];
        }
        else if(sp=='%'){tmp[tl++]='%';}
        else{tmp[tl++]='%';tmp[tl++]=sp;}
        tmp[tl]=0;
        if(!lft&&wid>tl){char fp=(zer&&sp!='s')?'0':' ';
            if(zer&&tl>0&&tmp[0]=='-'&&p<sz-1){b[p++]='-';for(int i=0;i<wid-tl&&p<sz-1;i++)b[p++]='0';for(int i=1;i<tl&&p<sz-1;i++)b[p++]=tmp[i];continue;}
            for(int i=0;i<wid-tl&&p<sz-1;i++)b[p++]=fp;}
        for(int i=0;i<tl&&p<sz-1;i++)b[p++]=tmp[i];
        if(lft&&wid>tl)for(int i=0;i<wid-tl&&p<sz-1;i++)b[p++]=' ';
    }
#undef _PC
    b[p]=0; return(int)p;
}
static inline int snprintf(char* b, size_t sz, const char* fmt, ...) {
    _va_list ap; _va_start(ap,fmt); int r=vsnprintf(b,sz,fmt,ap); _va_end(ap); return r;
}
static inline int sprintf(char* b, const char* fmt, ...) {
    _va_list ap; _va_start(ap,fmt); int r=vsnprintf(b,4096,fmt,ap); _va_end(ap); return r;
}
static inline int printf(const char* fmt, ...) {
    char pb[1024]; _va_list ap; _va_start(ap,fmt);
    int r=vsnprintf(pb,sizeof(pb),fmt,ap); _va_end(ap);
    if(r>0)write(1,pb,(size_t)r); return r;
}
static inline int fprintf(int fd, const char* fmt, ...) {
    char pb[1024]; _va_list ap; _va_start(ap,fmt);
    int r=vsnprintf(pb,sizeof(pb),fmt,ap); _va_end(ap);
    if(r>0)write(fd,pb,(size_t)r); return r;
}

/* ── Terminal (termios) ──────────────────────────────────────────────────── */
#define ISIG    0x0001u
#define ICANON  0x0002u
#define ECHO    0x0008u
#define ECHOE   0x0010u
#define VMIN    0
#define VTIME   1
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

typedef struct { unsigned int c_lflag; unsigned char c_cc[8]; } termios_t;

static inline long tcgetattr(int fd, termios_t* t) {
    return _syscall3(SYS_TCGETATTR,(long)fd,(long)t,0);
}
static inline long tcsetattr(int fd, int when, const termios_t* t) {
    return _syscall3(SYS_TCSETATTR,(long)fd,(long)t,(long)when);
}

/* ── readline with history and arrow-key editing ─────────────────────────── */
#define RL_MAX   256
#define RL_HIST  20

static char _rl_out[RL_MAX];
static char _rl_hist[RL_HIST][RL_MAX];
static int  _rl_nhist;

static inline void _rl_draw(const char* pr, int pl, const char* b,
                             int len, int cur, int old_len) {
    write(1,"\r",1); write(1,pr,(size_t)pl);
    if(len) write(1,b,(size_t)len);
    for(int i=len;i<old_len;i++) write(1," ",1);
    write(1,"\r",1); write(1,pr,(size_t)pl);
    if(cur) write(1,b,(size_t)cur);
}

/**
 * readline(prompt) — interactive line editor with history (20 entries).
 * Keys: printable=insert  BS/DEL=erase  ^A=home  ^E=end  ^K=kill-eol
 *       ESC[A/B=history↑↓  ESC[C/D=cursor←→  ESC[3~=Delete
 * Returns static buffer; saves/restores termios automatically.
 */
static inline char* readline(const char* prompt) {
    termios_t saved, raw;
    tcgetattr(0,&saved); raw=saved;
    raw.c_lflag &= ~(unsigned int)(ICANON|ECHO);
    raw.c_cc[VMIN]=1;
    tcsetattr(0,TCSANOW,&raw);

    int pl=(int)strlen(prompt);
    write(1,prompt,(size_t)pl);

    char buf[RL_MAX]; int len=0,cur=0,old_len=0;
    int hist_pos=_rl_nhist;
    char tmp[RL_MAX]; int tmp_len=0, in_hist=0;

    for(;;) {
        char c; read(0,&c,1);

        if(c=='\r'||c=='\n') {
            buf[len]=0; write(1,"\n",1);
            if(len>0) {
                int dup=(_rl_nhist>0 && strcmp(_rl_hist[_rl_nhist-1],buf)==0);
                if(!dup) {
                    if(_rl_nhist<RL_HIST) {
                        for(int i=0;i<len;i++) _rl_hist[_rl_nhist][i]=buf[i];
                        _rl_hist[_rl_nhist++][len]=0;
                    } else {
                        for(int i=0;i<RL_HIST-1;i++)
                            for(int j=0;j<RL_MAX;j++) _rl_hist[i][j]=_rl_hist[i+1][j];
                        for(int i=0;i<len;i++) _rl_hist[RL_HIST-1][i]=buf[i];
                        _rl_hist[RL_HIST-1][len]=0;
                    }
                }
            }
            break;
        }

        if(c==0x7F||c=='\b') {
            if(cur>0){for(int i=cur-1;i<len-1;i++)buf[i]=buf[i+1];
                old_len=len;len--;cur--;_rl_draw(prompt,pl,buf,len,cur,old_len);}
            continue;
        }
        if(c==0x01){_rl_draw(prompt,pl,buf,len,0,len);cur=0;continue;}
        if(c==0x05){_rl_draw(prompt,pl,buf,len,len,len);cur=len;continue;}
        if(c==0x0B){old_len=len;len=cur;_rl_draw(prompt,pl,buf,len,cur,old_len);continue;}

        if(c==0x1B) {
            char s1,s2; read(0,&s1,1);
            if(s1=='[') {
                read(0,&s2,1);
                if(s2=='D'&&cur>0){cur--;_rl_draw(prompt,pl,buf,len,cur,len);}
                else if(s2=='C'&&cur<len){cur++;_rl_draw(prompt,pl,buf,len,cur,len);}
                else if(s2=='A') {
                    if(!in_hist){for(int i=0;i<len;i++)tmp[i]=buf[i];
                        tmp[len]=0;tmp_len=len;in_hist=1;hist_pos=_rl_nhist;}
                    if(hist_pos>0){hist_pos--;
                        int hl=(int)strlen(_rl_hist[hist_pos]);old_len=len;
                        for(int i=0;i<hl;i++)buf[i]=_rl_hist[hist_pos][i];
                        buf[hl]=0;len=hl;cur=len;_rl_draw(prompt,pl,buf,len,cur,old_len);}
                }
                else if(s2=='B') {
                    if(in_hist&&hist_pos<_rl_nhist){hist_pos++;old_len=len;
                        if(hist_pos==_rl_nhist){for(int i=0;i<tmp_len;i++)buf[i]=tmp[i];
                            buf[tmp_len]=0;len=tmp_len;in_hist=0;}
                        else{int hl=(int)strlen(_rl_hist[hist_pos]);
                            for(int i=0;i<hl;i++)buf[i]=_rl_hist[hist_pos][i];buf[hl]=0;len=hl;}
                        cur=len;_rl_draw(prompt,pl,buf,len,cur,old_len);}
                }
                else if(s2=='3'){char tilde;read(0,&tilde,1);
                    if(cur<len){old_len=len;for(int i=cur;i<len-1;i++)buf[i]=buf[i+1];
                        len--;_rl_draw(prompt,pl,buf,len,cur,old_len);}}
            }
            continue;
        }

        if((unsigned char)c>=0x20&&len<RL_MAX-1) {
            if(in_hist)in_hist=0;
            for(int i=len;i>cur;i--)buf[i]=buf[i-1];
            buf[cur]=c;old_len=len;len++;cur++;
            _rl_draw(prompt,pl,buf,len,cur,old_len);
        }
    }

    tcsetattr(0,TCSANOW,&saved);
    for(int i=0;i<=len;i++) _rl_out[i]=buf[i];
    return _rl_out;
}

/* ── Heap allocation ─────────────────────────────────────────────────────── */
static inline long brk(long addr)    { return _syscall3(SYS_BRK,addr,0,0); }
static inline void* sbrk(long incr) {
    long cur=brk(0); if(!incr)return(void*)cur;
    long nb=brk(cur+incr); return(nb==cur+incr)?(void*)cur:(void*)-1;
}

typedef struct _nh { size_t size; unsigned int free; unsigned int _pad; } _nh_t;
#define _NH ((size_t)sizeof(_nh_t))

static inline void* malloc(size_t sz) {
    static char* _hb; static char* _he;
    if(!_hb){_hb=_he=(char*)sbrk(0);}
    if(!sz)return(void*)0;
    sz=(sz+15u)&~(size_t)15u;
    _nh_t* h=(_nh_t*)_hb;
    while((char*)h<_he){if(h->free&&h->size>=sz){h->free=0;return(void*)(h+1);}
        h=(_nh_t*)((char*)(h+1)+h->size);}
    size_t need=_NH+sz;
    if(sbrk((long)need)==(void*)-1)return(void*)0;
    h=(_nh_t*)_he;_he+=(long)need;
    h->size=sz;h->free=0;h->_pad=0;return(void*)(h+1);
}
static inline void  free(void* ptr)  { if(ptr)((_nh_t*)ptr-1)->free=1; }
static inline void* realloc(void* ptr, size_t sz) {
    if(!ptr)return malloc(sz); if(!sz){free(ptr);return(void*)0;}
    _nh_t* h=(_nh_t*)ptr-1; if(h->size>=sz)return ptr;
    void* n=malloc(sz); if(!n)return(void*)0;
    memcpy(n,ptr,h->size); free(ptr); return n;
}

#endif /* NOXLIB_H */
