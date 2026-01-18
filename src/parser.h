#ifndef DUSTH_PARSER_H
#define DUSTH_PARSER_H

#include <stddef.h>

typedef enum {
    NODE_PROGRAM,
    NODE_EXPR_STMT,
    NODE_LET,
    NODE_BLOCK,
    NODE_IF,
    NODE_LOOP,
    NODE_RETURN,
    NODE_BINARY,
    NODE_UNARY,
    NODE_LITERAL,
    NODE_IDENT,
    NODE_CALL,
    NODE_FUNC,
    NODE_INDEX,
    NODE_ASSIGN,
    NODE_EXTERN,
    NODE_STRING,
    NODE_ARRAY,
    NODE_IMPORT
} NodeType;

typedef struct Node Node;

struct Node {
    NodeType type;
    Node** children;
    size_t childc;
    size_t capacity;
    char* text;
    double num;
};

Node* parse_program(const char* src);
void free_node(Node* n);

#endif
