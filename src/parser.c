#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"
typedef struct {
    const char* src;
    size_t pos;
    size_t len;
    size_t line;
    size_t col;
} PState;
static char p_peek(PState* s){ if(s->pos>=s->len) return 0; return s->src[s->pos]; }
static char p_advance(PState* s){ char c=p_peek(s); if(c==0) return 0; s->pos++; if(c=='\n'){ s->line++; s->col=1; } else s->col++; return c; }
static void p_skip_ws(PState* s){ for(;;){ char c=p_peek(s); if(!c) return; if(c==' '||c=='\t'||c=='\r'||c=='\n'){ p_advance(s); continue; } if(c=='/'&&s->pos+1<s->len&&s->src[s->pos+1]=='/'){ p_advance(s); p_advance(s); while(p_peek(s) && p_peek(s)!='\n') p_advance(s); continue; } break; } }
static int is_ident_start(char c){ return isalpha((unsigned char)c)||c=='_'||c=='$'; }
static int is_ident_char(char c){ return isalnum((unsigned char)c)||c=='_'||c=='$'; }
static char* parse_ident(PState* s){ size_t st=s->pos; while(is_ident_char(p_peek(s))) p_advance(s); size_t en=s->pos; return dh_strndup(s->src+st,en-st); }
static char* parse_string(PState* s){ p_advance(s); size_t st=s->pos; while(p_peek(s) && p_peek(s)!='"'){ p_advance(s); } size_t en=s->pos; char* out=dh_strndup(s->src+st,en-st); if(p_peek(s)=='"') p_advance(s); return out; }
static double parse_number(PState* s){ size_t st=s->pos; int dot=0; if(p_peek(s)=='-') p_advance(s); while((p_peek(s)>='0'&&p_peek(s)<='9')||p_peek(s)=='.'){ if(p_peek(s)=='.') dot=1; p_advance(s); } size_t en=s->pos; char* t=dh_strndup(s->src+st,en-st); double v=strtod(t,NULL); free(t); return v; }
static Node* node_new(NodeType t){ Node* n=malloc(sizeof(Node)); n->type=t; n->children=NULL; n->childc=0; n->text=NULL; n->num=0; return n; }
static void node_append(Node* n, Node* c){ n->children=realloc(n->children,sizeof(Node*)*(n->childc+1)); n->children[n->childc++]=c; }
static Node* parse_expression(PState* s);
static Node* parse_primary(PState* s){
    p_skip_ws(s);
    char c=p_peek(s);
    if(is_ident_start(c)){
        char* id=parse_ident(s);
        p_skip_ws(s);
        if(p_peek(s)=='('){
            p_advance(s);
            Node* call=node_new(NODE_CALL);
            call->text=id;
            p_skip_ws(s);
            if(p_peek(s)!=')'){
                for(;;){
                    Node* arg=parse_expression(s);
                    node_append(call,arg);
                    p_skip_ws(s);
                    if(p_peek(s)==','){ p_advance(s); p_skip_ws(s); continue; }
                    break;
                }
            }
            if(p_peek(s)==')') p_advance(s);
            return call;
        }
        Node* n=node_new(NODE_IDENT);
        n->text=id;
        return n;
    }
    if(c=='"'){
        char* sstr=parse_string(s);
        Node* n=node_new(NODE_LITERAL);
        n->text=sstr;
        return n;
    }
    if((c>='0'&&c<='9')||c=='-'){
        double v=parse_number(s);
        Node* n=node_new(NODE_LITERAL);
        n->num=v;
        return n;
    }
    if(c=='('){
        p_advance(s);
        Node* e=parse_expression(s);
        if(p_peek(s)==')') p_advance(s);
        return e;
    }
    return node_new(NODE_LITERAL);
}
static int match_chars(PState* s, const char* token){
    p_skip_ws(s);
    size_t L=strlen(token);
    if(s->pos+L> s->len) return 0;
    if(strncmp(s->src+s->pos,token,L)==0){ s->pos+=L; return 1; }
    return 0;
}
static Node* parse_unary(PState* s){
    p_skip_ws(s);
    if(match_chars(s,"-")){
        Node* right=parse_unary(s);
        Node* n=node_new(NODE_UNARY);
        n->text=dh_strdup("-");
        node_append(n,right);
        return n;
    }
    return parse_primary(s);
}
static Node* parse_mul(PState* s){
    Node* left=parse_unary(s);
    for(;;){
        p_skip_ws(s);
        if(match_chars(s,"*")){ Node* r=parse_unary(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("*"); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"/")){ Node* r=parse_unary(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("/"); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"%")){ Node* r=parse_unary(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("%"); node_append(n,left); node_append(n,r); left=n; continue; }
        break;
    }
    return left;
}
static Node* parse_add(PState* s){
    Node* left=parse_mul(s);
    for(;;){
        p_skip_ws(s);
        if(match_chars(s,"+")){ Node* r=parse_mul(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("+"); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"-")){ Node* r=parse_mul(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("-"); node_append(n,left); node_append(n,r); left=n; continue; }
        break;
    }
    return left;
}
static Node* parse_comparison(PState* s){
    Node* left=parse_add(s);
    for(;;){
        p_skip_ws(s);
        if(match_chars(s,"==")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("=="); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"!=")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("!="); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"<=")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("<="); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,">=")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup(">="); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,"<")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup("<"); node_append(n,left); node_append(n,r); left=n; continue; }
        if(match_chars(s,">")){ Node* r=parse_add(s); Node* n=node_new(NODE_BINARY); n->text=dh_strdup(">"); node_append(n,left); node_append(n,r); left=n; continue; }
        break;
    }
    return left;
}
static Node* parse_assignment(PState* s){
    p_skip_ws(s);
    size_t save=s->pos;
    if(is_ident_start(p_peek(s))){
        char* id=parse_ident(s);
        p_skip_ws(s);
        if(match_chars(s,"=")){
            Node* rhs=parse_expression(s);
            Node* n=node_new(NODE_LET);
            n->text=id;
            node_append(n,rhs);
            return n;
        } else {
            s->pos=save;
        }
    }
    return parse_comparison(s);
}
static Node* parse_expression(PState* s){
    return parse_assignment(s);
}
static Node* parse_statement(PState* s){
    p_skip_ws(s);
    if(match_chars(s,"let")){
        p_skip_ws(s);
        char* id=parse_ident(s);
        p_skip_ws(s);
        match_chars(s,"=");
        Node* rhs=parse_expression(s);
        if(p_peek(s)==';') p_advance(s);
        Node* n=node_new(NODE_LET);
        n->text=id;
        node_append(n,rhs);
        return n;
    }
    if(match_chars(s,"extern")){
        p_skip_ws(s);
        if(p_peek(s)=='\"'){ char* sname=parse_string(s); Node* n=node_new(NODE_EXTERN); n->text=sname; return n; }
    }
    Node* e=parse_expression(s);
    if(p_peek(s)==';') p_advance(s);
    Node* stmt=node_new(NODE_EXPR_STMT);
    node_append(stmt,e);
    return stmt;
}
Node* parse_program(const char* src){
    PState s; s.src=src; s.pos=0; s.len=strlen(src); s.line=1; s.col=1;
    Node* root=node_new(NODE_PROGRAM);
    for(;;){
        p_skip_ws(&s);
        if(s.pos>=s.len) break;
        Node* st=parse_statement(&s);
        node_append(root,st);
    }
    return root;
}
void free_node(Node* n){ if(!n) return; for(size_t i=0;i<n->childc;i++) free_node(n->children[i]); free(n->children); if(n->text) free(n->text); free(n); }