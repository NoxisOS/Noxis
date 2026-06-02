/**
 * @file    userland/nsh.c
 * @brief   nsh — Noxis Shell
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* ── TTY raw mode ──────────────────────────────────────────── */
#define TCGETS  1
#define TCSETS  2
#define ISIG    0x01
#define ICANON  0x02
#define ECHO    0x04
typedef struct { unsigned int lflag; } termios_t;

/* ── Limits ────────────────────────────────────────────────── */
#define MAX_LINE    512
#define MAX_TOKENS  128
#define MAX_STAGES  8
#define MAX_ARGS    32
#define MAX_CWD     256
#define DIRENT_SZ   32
#define FT_DIR      2

typedef struct __attribute__((packed)) {
    uint32_t inode; uint16_t rec_len;
    uint8_t name_len, file_type;
    char name[24];
} dirent_t;

typedef struct {
    char *argv[MAX_ARGS+1]; int argc;
    char *redir_in, *redir_out; int append;
} stage_t;
typedef struct {
    stage_t stages[MAX_STAGES]; int nstages, background;
} pipeline_t;

static char g_cwd[MAX_CWD] = "/";
static int  g_last_status  = 0;
static volatile int g_sigint = 0;

static void _sigint_handler(int sig) { (void)sig; g_sigint=1; }

/* ══════════════════════════════════════════════════════════════
 * Tokenizer
 * ══════════════════════════════════════════════════════════════ */
static int tokenize(char *line, char **toks, int max) {
    int len=(int)strlen(line);
    while(len>0&&(line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==' '||line[len-1]=='\t')) line[--len]=0;
    int n=0; char *p=line;
    while(n<max-1&&*p){
        while(*p==' '||*p=='\t')p++;
        if(!*p||*p=='#')break;
        if(p[0]=='>'&&p[1]=='>'){toks[n++]=">>";p+=2;continue;}
        switch(*p){case'|':toks[n++]="|";p++;continue;case'>':toks[n++]=">";p++;continue;case'<':toks[n++]="<";p++;continue;case'&':toks[n++]="&";p++;continue;}
        toks[n++]=p; while(*p&&*p!=' '&&*p!='\t'&&*p!='|'&&*p!='>'&&*p!='<'&&*p!='&'&&*p!='#')p++;
        if(*p=='#'){*p=0;break;} if(*p)*p++=0;
    }
    toks[n]=NULL; return n;
}

/* ══════════════════════════════════════════════════════════════
 * Parser
 * ══════════════════════════════════════════════════════════════ */
static char g_status_str[16];
static int parse(char **toks, int ntoks, pipeline_t *pl) {
    memset(pl,0,sizeof(*pl)); if(ntoks==0)return 0;
    snprintf(g_status_str,sizeof(g_status_str),"%d",g_last_status);
    int si=0; stage_t *st=&pl->stages[0];
    for(int i=0;i<ntoks;i++){char*t=toks[i];
        if(!strcmp(t,"|")){st->argv[st->argc]=NULL;if(++si>=MAX_STAGES){printf("nsh: too many pipes\n");return-1;}st=&pl->stages[si];}
        else if(!strcmp(t,">")||!strcmp(t,">>")){if(i+1>=ntoks){printf("nsh: missing filename\n");return-1;}st->redir_out=toks[++i];st->append=(t[1]=='>');}
        else if(!strcmp(t,"<")){if(i+1>=ntoks){printf("nsh: missing filename\n");return-1;}st->redir_in=toks[++i];}
        else if(!strcmp(t,"&"))pl->background=1;
        else{if(!strcmp(t,"$?"))t=g_status_str;if(st->argc<MAX_ARGS)st->argv[st->argc++]=t;}
    }
    st->argv[st->argc]=NULL; pl->nstages=si+1;
    if(pl->stages[0].argc==0){pl->nstages=0;return 0;} return 0;
}

/* ══════════════════════════════════════════════════════════════
 * Tab completion — list files matching prefix
 * ══════════════════════════════════════════════════════════════ */
static void tab_complete(char *line, int *pos) {
    /* Extract the word being typed */
    int end=*pos, start=end;
    while(start>0 && line[start-1]!=' ' && line[start-1]!='\t') start--;
    int wlen=end-start; if(wlen==0)return;
    char word[128]; memcpy(word,line+start,wlen); word[wlen]=0;

    /* List matching files/dirs from cwd */
    char buf[DIRENT_SZ*128];
    int n=getdents(99,buf,sizeof(buf));
    if(n<=0)return;
    dirent_t *de=(dirent_t*)buf; int cnt=n/DIRENT_SZ;
    char matches[64][32]; int mcount=0;

    for(int i=0;i<cnt;i++,de++){
        if(de->inode==0||de->name_len==0)continue;
        int nl=de->name_len<24?de->name_len:24;
        char nm[25]; memcpy(nm,de->name,nl); nm[nl]=0;
        if(!strcmp(nm,".")||!strcmp(nm,".."))continue;
        if(!strncmp(nm,word,wlen)){
            if(mcount<64){strncpy(matches[mcount],nm,31);matches[mcount++][31]=0;}
        }
    }
    if(mcount==0)return;

    /* If single match: auto-complete */
    if(mcount==1){
        int mlen=strlen(matches[0]);
        for(int i=wlen;i<mlen;i++){line[start+i]=matches[0][i];putchar(matches[0][i]);}
        *pos=start+mlen;
        putchar(' '); line[*pos]=' '; (*pos)++;
        return;
    }

    /* Multiple matches: list them */
    putchar('\n');
    for(int i=0;i<mcount;i++){
        printf("  %s",matches[i]);
        /* Find common prefix */
        if(i>0){int cp=0;while(cp<wlen&&matches[i][cp]==matches[0][cp])cp++;wlen=cp;}
    }
    putchar('\n');
    /* Reprint prompt + line so far */
    printf("nsh %s > ", g_cwd);
    for(int i=0;i<*pos;i++)putchar(line[i]);
}

/* ══════════════════════════════════════════════════════════════
 * Line editor with tab completion
 * ══════════════════════════════════════════════════════════════ */
static int readline(char *buf, int max) {
    /* Switch to raw mode so we get each keypress immediately */
    termios_t saved, raw;
    ioctl(STDIN_FILENO, TCGETS, &saved);
    raw.lflag = saved.lflag & ~(ICANON | ECHO);
    ioctl(STDIN_FILENO, TCSETS, &raw);

    int pos=0; buf[0]=0;
    for(;;){
        char c;
        int n=read(STDIN_FILENO,&c,1);
        if(n<=0){if(g_sigint){ioctl(STDIN_FILENO,TCSETS,&saved);return -1;}continue;}
        if(g_sigint){ioctl(STDIN_FILENO,TCSETS,&saved);return -1;}

        if(c=='\n'||c=='\r'){
            buf[pos]=0;
            putchar('\n');
            break;
        } else if(c=='\t'){
            tab_complete(buf,&pos);
        } else if(c=='\b'||c==127){
            if(pos>0){pos--;buf[pos]=0;putchar('\b');putchar(' ');putchar('\b');}
        } else if(c==0x04&&pos==0){
            ioctl(STDIN_FILENO,TCSETS,&saved);
            return -2; /* Ctrl+D = EOF */
        } else if(c==0x03){
            g_sigint=1;
            ioctl(STDIN_FILENO,TCSETS,&saved);
            return -1; /* Ctrl+C */
        } else if((unsigned char)c>=32&&(unsigned char)c<127&&pos<max-1){
            buf[pos++]=c; buf[pos]=0; putchar(c);
        }
    }

    ioctl(STDIN_FILENO, TCSETS, &saved);
    return pos;
}

/* ══════════════════════════════════════════════════════════════
 * Builtins
 * ══════════════════════════════════════════════════════════════ */
static int builtin_ls(void) {
    char buf[DIRENT_SZ*128];
    int n=getdents(99,buf,(int)sizeof(buf));
    if(n<0){printf("ls: cannot read directory\n");return 1;}
    dirent_t *de=(dirent_t*)buf; int cnt=n/DIRENT_SZ;
    for(int i=0;i<cnt;i++,de++){
        if(de->inode==0)continue;
        int nl=de->name_len<24?de->name_len:24; char nm[25]; memcpy(nm,de->name,nl);nm[nl]=0;
        if(!strcmp(nm,".")||!strcmp(nm,".."))continue;
        /* Synfs dirs already carry a leading '/' in nm (/proc, /dev).
           Regular NoxFS dirs don't (test, etc). Print as-is + trailing /. */
        if(de->file_type==FT_DIR) printf("  %s/\n",nm);
        else                      printf("  %s\n",nm);
    }
    return 0;
}

static int builtin_cat(stage_t *st) {
    if(st->argc<2){printf("cat: missing file\n");return 1;}
    for(int i=1;i<st->argc;i++){
        int fd=open(st->argv[i],O_RDONLY);
        if(fd<0){printf("cat: %s: not found\n",st->argv[i]);return 1;}
        char buf[256]; int n;
        while((n=(int)read(fd,buf,sizeof(buf)))>0)write(STDOUT_FILENO,buf,n);
        close(fd);
    }
    return 0;
}

static int builtin_rm(stage_t *st) {
    if(st->argc<2){printf("rm: missing file\n");return 1;}
    for(int i=1;i<st->argc;i++){
        if(unlink(st->argv[i])<0){
            printf("rm: %s: failed\n",st->argv[i]);
            return 1;
        }
    }
    return 0;
}

static int builtin_cp(stage_t *st) {
    if(st->argc<3){printf("cp: src dst\n");return 1;}
    int sfd=open(st->argv[1],O_RDONLY);
    if(sfd<0){printf("cp: cannot open source\n");return 1;}
    int dfd=creat(st->argv[2]);
    if(dfd<0){close(sfd);printf("cp: cannot create dest\n");return 1;}
    char buf[256]; int n;
    while((n=(int)read(sfd,buf,sizeof(buf)))>0)write(dfd,buf,n);
    close(sfd);close(dfd);
    return 0;
}

static int builtin_mv(stage_t *st) {
    if(st->argc<3){printf("mv: src dst\n");return 1;}
    if(rename(st->argv[1],st->argv[2])<0){
        printf("mv: failed\n");return 1;
    }
    return 0;
}

static int exec_builtin(stage_t *st) {
    if(st->argc==0)return 0;
    char *cmd=st->argv[0];

    if(!strcmp(cmd,"cd")){
        const char *raw=(st->argc>1)?st->argv[1]:"/";
        if(chdir(raw)<0){printf("cd: %s: no such directory\n",raw);return 1;}
        char dir[MAX_CWD]; int dl=0;
        while(raw[dl]&&dl<MAX_CWD-1){dir[dl]=raw[dl];dl++;}
        while(dl>1&&dir[dl-1]=='/')dl--;
        dir[dl]=0;
        if(dir[0]=='/'){strncpy(g_cwd,dir,MAX_CWD-1);g_cwd[MAX_CWD-1]=0;}
        else if(!strcmp(dir,"..")){int l=(int)strlen(g_cwd);while(l>1&&g_cwd[l-1]!='/')l--;if(l>1)l--;if(l==0)l=1;g_cwd[l]=0;}
        else if(strcmp(dir,".")){int l=(int)strlen(g_cwd);if(l>1&&g_cwd[l-1]!='/')strncat(g_cwd,"/",MAX_CWD-l-1);strncat(g_cwd,dir,MAX_CWD-(int)strlen(g_cwd)-1);}
        return 0;
    }
    if(!strcmp(cmd,"pwd")){printf("%s\n",g_cwd);return 0;}
    if(!strcmp(cmd,"cat"))return builtin_cat(st);
    if(!strcmp(cmd,"rm"))return builtin_rm(st);
    if(!strcmp(cmd,"cp"))return builtin_cp(st);
    if(!strcmp(cmd,"mv"))return builtin_mv(st);
    if(!strcmp(cmd,"mkdir")){
        if(st->argc<2){printf("mkdir: missing name\n");return 1;}
        for(int i=1;i<st->argc;i++){if(mkdir(st->argv[i])<0){printf("mkdir: %s: failed\n",st->argv[i]);}}
        return 0;
    }
    if(!strcmp(cmd,"ls"))return builtin_ls();
    if(!strcmp(cmd,"echo")){for(int i=1;i<st->argc;i++){if(i>1)putchar(' ');printf("%s",st->argv[i]);}putchar('\n');return 0;}
    if(!strcmp(cmd,"clear")){
        for(int i=0;i<25;i++) putchar('\n');
        return 0;
    }
    if(!strcmp(cmd,"exit")){exit((st->argc>1)?atoi(st->argv[1]):0);return 0;}
    if(!strcmp(cmd,"keymap")){
        if(st->argc<2){int fd=open("/proc/keymap",O_RDONLY);if(fd<0){printf("keymap: unavailable\n");return 1;}char buf[128];int n;while((n=(int)read(fd,buf,sizeof(buf)))>0)write(STDOUT_FILENO,buf,n);close(fd);return 0;}
        int fd=open("/dev/keymap",O_RDONLY);if(fd<0){printf("keymap: cannot open\n");return 1;}write(fd,st->argv[1],(int)strlen(st->argv[1]));close(fd);printf("keymap: switched to %s\n",st->argv[1]);return 0;
    }
    if(!strcmp(cmd,"help")){
        printf("\n  nsh - Noxis Shell\n");
        printf("  \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\n");
        printf("  cd [dir]       change directory\n");
        printf("  pwd            print working directory\n");
        printf("  ls             list files\n");
        printf("  cat <file>     print file contents\n");
        printf("  mkdir <dir>    create directory\n");
        printf("  rm <file>      remove file\n");
        printf("  cp <src> <dst> copy file\n");
        printf("  mv <src> <dst> move/rename file\n");
        printf("  echo [args]    print text\n");
        printf("  clear          clear screen\n");
        printf("  keymap [name]  switch keyboard (us/fr)\n");
        printf("  exit [n]       exit shell\n");
        printf("\n  Syntax: cmd | cmd2, cmd > file, cmd < file, cmd &\n\n");
        return 0;
    }
    return -1;
}

/* ══════════════════════════════════════════════════════════════
 * Child: pipes + redirections + exec
 * ══════════════════════════════════════════════════════════════ */
static void child_exec(stage_t *st, int pipes[][2], int idx, int nstages) {
    if(idx>0)dup2(pipes[idx-1][0],STDIN_FILENO);
    if(idx<nstages-1)dup2(pipes[idx][1],STDOUT_FILENO);
    for(int j=0;j<nstages-1;j++){close(pipes[j][0]);close(pipes[j][1]);}
    if(st->redir_in){int fd=open(st->redir_in,O_RDONLY);if(fd<0){printf("nsh: cannot open %s\n",st->redir_in);_exit(1);}dup2(fd,STDIN_FILENO);close(fd);}
    if(st->redir_out){int fd;if(st->append){fd=open(st->redir_out,O_RDWR);if(fd<0)fd=creat(st->redir_out);else lseek(fd,0,SEEK_END);}else fd=creat(st->redir_out);if(fd<0){printf("nsh: cannot open %s\n",st->redir_out);_exit(1);}dup2(fd,STDOUT_FILENO);close(fd);}
    signal(SIGINT,SIG_DFL);
    int r=exec_builtin(st); if(r>=0)_exit(r);
    execv(st->argv[0],st->argv);
    char elf[64]; snprintf(elf,sizeof(elf),"%s.elf",st->argv[0]); execv(elf,st->argv);
    printf("nsh: %s: command not found\n",st->argv[0]);
    _exit(127);
}

/* ══════════════════════════════════════════════════════════════
 * Pipeline execution
 * ══════════════════════════════════════════════════════════════ */
static int exec_pipeline(pipeline_t *pl) {
    if(pl->nstages==0)return 0;
    if(pl->nstages==1){char *cmd=pl->stages[0].argv[0];if(cmd&&(!strcmp(cmd,"cd")||!strcmp(cmd,"exit")))return exec_builtin(&pl->stages[0]);}
    if(pl->nstages==1&&!pl->stages[0].redir_in&&!pl->stages[0].redir_out&&!pl->background){int r=exec_builtin(&pl->stages[0]);if(r>=0)return r;}
    int n=pl->nstages,pipes[MAX_STAGES-1][2]; pid_t pids[MAX_STAGES];
    for(int i=0;i<n-1;i++){if(pipe(pipes[i])<0){printf("nsh: pipe() failed\n");return 1;}}
    for(int i=0;i<n;i++){pids[i]=fork();if(pids[i]<0){printf("nsh: fork() failed\n");for(int j=0;j<n-1;j++){close(pipes[j][0]);close(pipes[j][1]);}return 1;}if(pids[i]==0)child_exec(&pl->stages[i],pipes,i,n);}
    for(int i=0;i<n-1;i++){close(pipes[i][0]);close(pipes[i][1]);}
    int status=0;
    if(!pl->background){for(int i=0;i<n;i++){int s=waitpid(pids[i],NULL,0);if(i==n-1)status=s;}}
    else printf("[%d] running\n",(int)pids[0]);
    return status;
}

/* ══════════════════════════════════════════════════════════════
 * Main REPL
 * ══════════════════════════════════════════════════════════════ */
int main(int argc, char **argv) {
    (void)argc;(void)argv;
    signal(SIGINT,_sigint_handler);
    char line[MAX_LINE], *toks[MAX_TOKENS]; pipeline_t pl;

    printf("\n  \xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n");
    printf("  \xBA  Noxis Shell - nsh         \xBA\n");
    printf("  \xBA  type 'help' for commands  \xBA\n");
    printf("  \xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\n\n");

    for(;;){
        if(g_sigint){g_sigint=0;g_last_status=130;putchar('\n');continue;}
        printf("nsh %s > ", g_cwd);
        int r=readline(line,MAX_LINE);
        if(r==-1){putchar('\n');continue;}  /* Ctrl+C */
        if(r==-2){putchar('\n');break;}      /* Ctrl+D */
        if(r==0)continue;
        char *p=line;while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r')p++;if(!*p)continue;
        int ntoks=tokenize(line,toks,MAX_TOKENS); if(ntoks==0)continue;
        if(parse(toks,ntoks,&pl)<0)continue;
        if(pl.nstages==0)continue;
        g_last_status=exec_pipeline(&pl);
    }
    return g_last_status;
}
