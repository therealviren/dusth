#include "parser.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <signal.h>

typedef struct { const char* start; const char* current; } Parser;
static Parser parser;

static void error(const char* message);
static void* safe_malloc(size_t size);
static char* safe_strdup(const char* s, size_t len);
static Node* new_node(NodeType type);
static void add_child(Node* parent, Node* child);
static char peek();
static char advance();
static bool is_at_end();
static void skip_whitespace();
static bool check(const char* keyword);
static bool match_keyword(const char* keyword);
static bool match_char(char expected);
static void expect_char(char expected, const char* err_msg);
static Node* parse_expr();
static Node* parse_stmt();
static Node* parse_block();
static Node* parse_primary();
static Node* parse_primary_with_postfix();
static Node* parse_postfix(Node* left);
static Node* parse_unary();
static Node* parse_factor();
static Node* parse_term();
static Node* parse_comparison();
static Node* parse_function();
static Node* parse_extern();
static Node* parse_import();

static void handle_interrupt(int sig) { (void)sig; exit(0); }

static void error(const char* message) {
    fprintf(stderr, "Parse Error: %s\n", message);
    fprintf(stderr, "Position: %.40s\n", parser.current);
    exit(1);
}

static void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) error("Memory allocation failed");
    return ptr;
}

static char* safe_strdup(const char* s, size_t len) {
    char* str = safe_malloc(len + 1);
    memcpy(str, s, len);
    str[len] = '\0';
    return str;
}

static Node* new_node(NodeType type) {
    Node* node = safe_malloc(sizeof(Node));
    node->type = type;
    node->children = NULL;
    node->childc = 0;
    node->capacity = 0;
    node->text = NULL;
    node->num = 0.0;
    return node;
}

static void add_child(Node* parent, Node* child) {
    if (!child) return;
    if (parent->childc + 1 > parent->capacity) {
        size_t new_cap = parent->capacity < 8 ? 8 : parent->capacity * 2;
        Node** new_children = safe_malloc(sizeof(Node*) * new_cap);
        if (parent->children) {
            memcpy(new_children, parent->children, sizeof(Node*) * parent->childc);
            free(parent->children);
        }
        parent->children = new_children;
        parent->capacity = new_cap;
    }
    parent->children[parent->childc++] = child;
}

void free_node(Node* n) {
    if (!n) return;
    for (size_t i = 0; i < n->childc; i++) free_node(n->children[i]);
    if (n->children) free(n->children);
    if (n->text) free(n->text);
    free(n);
}

static char peek() { return *parser.current; }

static char advance() {
    if (is_at_end()) return '\0';
    return *parser.current++;
}

static bool is_at_end() { return *parser.current == '\0'; }

static void skip_whitespace() {
    for (;;) {
        char c = peek();
        if (isspace((unsigned char)c)) advance();
        else if (c == '/' && parser.current[1] == '/') {
            while (peek() != '\n' && !is_at_end()) advance();
        } else break;
    }
}

static bool check(const char* keyword) {
    size_t len = strlen(keyword);
    if (strncmp(parser.current, keyword, len) == 0) {
        char after = parser.current[len];
        return !isalnum((unsigned char)after) && after != '_';
    }
    return false;
}

static bool match_keyword(const char* keyword) {
    skip_whitespace();
    if (check(keyword)) {
        parser.current += strlen(keyword);
        return true;
    }
    return false;
}

static bool match_char(char expected) {
    skip_whitespace();
    if (peek() == expected) {
        advance();
        return true;
    }
    return false;
}

static void expect_char(char expected, const char* err_msg) {
    if (!match_char(expected)) error(err_msg);
}

Node* node_clone(Node* n){
    if(!n) return NULL;
    Node* c = malloc(sizeof(Node));
    c->type = n->type;
    c->childc = n->childc;
    c->capacity = n->childc;
    c->text = n->text ? safe_strdup(n->text, strlen(n->text)) : NULL;
    c->num = n->num;
    c->children = NULL;
    if(n->childc){
        c->children = malloc(sizeof(Node*) * n->childc);
        for(size_t i = 0; i < n->childc; i++) c->children[i] = node_clone(n->children[i]);
    }
    return c;
}

static Node* parse_primary() {
    skip_whitespace();
    char c = peek();
    if (c == '"') {
        advance();
        const char* start = parser.current;
        while (peek() != '"' && !is_at_end()) advance();
        size_t len = parser.current - start;
        Node* node = new_node(NODE_LITERAL);
        node->text = safe_strdup(start, len);
        expect_char('"', "Expected closing '\"'");
        return node;
    }
    if (c == '(') {
        advance();
        Node* inner = parse_expr();
        expect_char(')', "Expected ')'");
        return inner;
    }
    if (isdigit((unsigned char)c)) {
        const char* start = parser.current;
        while (isdigit((unsigned char)peek()) || peek() == '.') advance();
        Node* node = new_node(NODE_LITERAL);
        node->num = strtod(start, NULL);
        return node;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        const char* start = parser.current;
        while (isalnum((unsigned char)peek()) || peek() == '_') advance();
        char* id = safe_strdup(start, parser.current - start);
        Node* node = new_node(NODE_IDENT);
        node->text = id;
        return node;
    }
    return NULL;
}

static Node* make_call_node_from_expr(Node* callee) {
    Node* n = new_node(NODE_CALL);
    n->text = NULL;
    add_child(n, callee);
    return n;
}

static Node* parse_postfix(Node* left) {
    for (;;) {
        skip_whitespace();
        if (peek() == '[') {
            advance();
            Node* idx = parse_expr();
            expect_char(']', "Expected ']'");
            Node* n = new_node(NODE_INDEX);
            add_child(n, left);
            add_child(n, idx);
            left = n;
            continue;
        }
        if (peek() == '(') {
            advance();
            Node* call = make_call_node_from_expr(left);
            skip_whitespace();
            if (peek() != ')') {
                while (!is_at_end() && peek() != ')') {
                    Node* arg = parse_expr();
                    if (!arg) break;
                    add_child(call, arg);
                    skip_whitespace();
                    if (!match_char(',')) break;
                }
            }
            expect_char(')', "Expected ')'");
            left = call;
            continue;
        }
        break;
    }
    return left;
}

static Node* parse_primary_with_postfix() {
    Node* p = parse_primary();
    if (!p) return NULL;
    return parse_postfix(p);
}

static Node* parse_unary() {
    if (match_char('-')) {
        Node* node = new_node(NODE_UNARY);
        node->text = safe_strdup("-",1);
        add_child(node, parse_unary());
        return node;
    }
    if (match_char('!')) {
        Node* node = new_node(NODE_UNARY);
        node->text = safe_strdup("!",1);
        add_child(node, parse_unary());
        return node;
    }
    return parse_primary_with_postfix();
}

static Node* parse_factor() {
    Node* left = parse_unary();
    while (true) {
        if (match_char('*')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("*",1);
            add_child(n,left);
            add_child(n,parse_unary());
            left = n;
        } else if (match_char('/')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("/",1);
            add_child(n,left);
            add_child(n,parse_unary());
            left = n;
        } else break;
    }
    return left;
}

static Node* parse_term() {
    Node* left = parse_factor();
    while (true) {
        if (match_char('+')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("+",1);
            add_child(n,left);
            add_child(n,parse_factor());
            left = n;
        } else if (match_char('-')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("-",1);
            add_child(n,left);
            add_child(n,parse_factor());
            left = n;
        } else break;
    }
    return left;
}

static Node* parse_comparison() {
    Node* left = parse_term();
    skip_whitespace();
    if (match_char('<')) {
        Node* n = new_node(NODE_BINARY);
        n->text = match_char('=')?safe_strdup("<=",2):safe_strdup("<",1);
        add_child(n,left);
        add_child(n,parse_term());
        left = n;
    } else if (match_char('>')) {
        Node* n = new_node(NODE_BINARY);
        n->text = match_char('=')?safe_strdup(">=",2):safe_strdup(">",1);
        add_child(n,left);
        add_child(n,parse_term());
        left = n;
    } else if (match_char('=')) {
        if (match_char('=')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("==",2);
            add_child(n,left);
            add_child(n,parse_term());
            left = n;
        } else {
            Node* n = new_node(NODE_ASSIGN);
            add_child(n,left);
            add_child(n,parse_expr());
            left = n;
        }
    } else if (match_char('!')) {
        if (match_char('=')) {
            Node* n = new_node(NODE_BINARY);
            n->text = safe_strdup("!=",2);
            add_child(n,left);
            add_child(n,parse_term());
            left = n;
        } else {
        }
    }
    return left;
}

static Node* parse_expr() { return parse_comparison(); }

static Node* parse_block() {
    expect_char('{',"Block must start with '{'");
    Node* block = new_node(NODE_BLOCK);
    while (peek() != '}' && !is_at_end()) add_child(block,parse_stmt());
    expect_char('}',"Block must end with '}'");
    return block;
}

static Node* parse_import() {
    skip_whitespace();
    if (peek() != '"') error("import expects a file string");
    advance();
    const char* start = parser.current;
    while (peek() != '"' && !is_at_end()) advance();
    Node* n = new_node(NODE_IMPORT);
    n->text = safe_strdup(start, parser.current - start);
    expect_char('"', "Unterminated import string");
    match_char(';');
    return n;
}

static Node* parse_function() {
    skip_whitespace();
    Node* node = new_node(NODE_FUNC);
    const char* start = parser.current;
    while (isalnum((unsigned char)peek()) || peek() == '_') advance();
    if (start == parser.current) error("Function must have a name");
    node->text = safe_strdup(start, parser.current - start);
    skip_whitespace();
    expect_char('(', "Function parameters must start with '('");
    if (peek() != ')') {
        do {
            skip_whitespace();
            const char* arg_start = parser.current;
            while (isalnum((unsigned char)peek()) || peek() == '_') advance();
            if (arg_start == parser.current) error("Function parameter name expected");
            Node* arg = new_node(NODE_IDENT);
            arg->text = safe_strdup(arg_start, parser.current - arg_start);
            add_child(node, arg);
        } while(match_char(','));
    }
    expect_char(')', "Function parameters must end with ')'");
    skip_whitespace();
    Node* body = parse_block();
    add_child(node, body);
    return node;
}

static Node* parse_extern() {
    skip_whitespace();
    Node* node = new_node(NODE_EXTERN);
    const char* start = parser.current;
    while (isalnum((unsigned char)peek())||peek()=='_') advance();
    if (start == parser.current) error("Extern must have a name");
    node->text = safe_strdup(start, parser.current - start);
    skip_whitespace();
    expect_char('(', "Extern parameters must start with '('");
    if (peek() != ')') {
        do {
            skip_whitespace();
            const char* arg_start = parser.current;
            while (isalnum((unsigned char)peek()) || peek() == '_') advance();
            if (arg_start == parser.current) error("Extern parameter name expected");
            Node* arg = new_node(NODE_IDENT);
            arg->text = safe_strdup(arg_start, parser.current - arg_start);
            add_child(node, arg);
        } while(match_char(','));
    }
    expect_char(')', "Extern parameters must end with ')'");
    match_char(';');
    return node;
}

static Node* parse_stmt() {
    skip_whitespace();
    if (is_at_end()) return NULL;
    Node* node = NULL;
    if (match_keyword("let")) {
        node = new_node(NODE_LET);
        skip_whitespace();
        const char* start = parser.current;
        while (isalnum((unsigned char)peek())||peek()=='_') advance();
        if (start == parser.current) error("Expected variable name after let");
        node->text = safe_strdup(start, parser.current-start);
        skip_whitespace();
        expect_char('=',"Expected '=' after variable name");
        add_child(node,parse_expr());
    } else if (match_keyword("if")) {
        skip_whitespace();
        Node* cond = NULL;
        if (peek() == '(') {
            advance();
            cond = parse_expr();
            expect_char(')', "if expects ')'");
        } else {
            cond = parse_expr();
            if (!cond) error("if expects a condition");
        }
        Node* thenb = parse_block();
        Node* n = new_node(NODE_IF);
        add_child(n, cond);
        add_child(n, thenb);
        skip_whitespace();
        if (match_keyword("else")) {
            skip_whitespace();
            if (peek() == '{') {
                Node* elseb = parse_block();
                add_child(n, elseb);
            } else {
                Node* elseStmt = parse_stmt();
                if (elseStmt) add_child(n, elseStmt);
            }
        }
        node = n;
    } else if (match_keyword("while")) {
        skip_whitespace();
        if (peek() == '(') {
            advance();
            Node* cond = parse_expr();
            expect_char(')', "while expects ')'");
            Node* body = parse_block();
            Node* n = new_node(NODE_LOOP);
            add_child(n, cond);
            add_child(n, body);
            node = n;
        } else {
            error("while expects '('");
        }
    } else if (match_keyword("return")) {
        Node* n = new_node(NODE_RETURN);
        skip_whitespace();
        if (peek() != ';') add_child(n, parse_expr());
        node = n;
    } else {
        Node* expr = parse_expr();
        if (expr) {
            node = new_node(NODE_EXPR_STMT);
            add_child(node, expr);
        }
    }
    skip_whitespace();
    match_char(';');
    return node;
}

Node* parse_program(const char* src) {
    signal(SIGINT, handle_interrupt);
    parser.start = src;
    parser.current = src;
    Node* program = new_node(NODE_PROGRAM);
    skip_whitespace();
    while (!is_at_end()) {
        Node* n = NULL;
        if (match_keyword("import")) n = parse_import();
        else if (match_keyword("fn")) n = parse_function();
        else if (match_keyword("extern")) n = parse_extern();
        else n = parse_stmt();
        if (n) add_child(program, n);
        skip_whitespace();
    }
    return program;
}