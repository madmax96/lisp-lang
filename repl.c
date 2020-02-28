#include <stdio.h>
#include <stdlib.h>
#include "mpc.h"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
/* We do not need editline to suport editing of text in terminal on windows,
   that is the default behaviour.*/
#include <string.h>
// if on windows compile this code
static char buffer[2048];
// fake readline function
char* readline(char* prompt){
    fputs(prompt, stdout);
    fgets(buffer,2048,stdin);
    char* cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy,buffer);
    cpy[strlen(cpy)-1] = '\0';
    return cpy;
}
void add_history(char* c){} // not needed if on windows
#else
#include <editline/readline.h>
#endif

//Macro for reusable error handling
#define LVAL_ASSERT(args,cond,fmt,...) if(!(cond)) { lval* err = lval_err(fmt, ##__VA_ARGS__); lval_free(args); return err; }

enum LVAL_T {LVAL_NUM ,LVAL_DECIMAL_NUM,LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUNC, LVAL_ERR};
enum EVAL_ERR {DIV_ZERO, BAD_OPERATOR, BAD_NUM};

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
typedef lval* (*lbuiltin) (lenv*,lval*);
struct lval{
  int type;
  union {
    long num;
    double decimal_num;
    char* sym;
    char* err;
    lbuiltin builtin;
  } value;

  // for user-defined functions
  lenv* env;
  lval* args;
  lval* body;
  int count;
  struct lval** cell;
};
struct lenv{
  int count;
  char** syms;
  lval** vals;
  lenv* parent_env;
};

lval* eval (lval* lv);
lval* lval_pop (lval* lv, int i);
void lval_print(lval* v);
lval* lval_eval(lenv* env, lval* v);
void lenv_free(lenv* env);
lenv* lenv_new(void);

lenv* lenv_copy(lenv* env);
char* ltype_name(int t) {
  switch(t) {
    case LVAL_FUNC: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

// lval type constructors
lval* lval_num(long x){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->value.num = x;
    return v;
}

lval* lval_err(char* fmt, ...){

    lval* err = malloc(sizeof(lval));
    err->type = LVAL_ERR;

    va_list arguments;
    va_start(arguments,fmt);

    err->value.err = malloc(512);

    // we use va_arg(arguments,<type>) to get next argument
    vsnprintf(err->value.err,511,fmt,arguments);
    err->value.err = realloc(err->value.err,strlen(err->value.err) + 1);
    va_end(arguments);
    return err;
}

lval* lval_sym(char* sym){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->value.sym = malloc(strlen(sym) + 1);
    strcpy(v->value.sym, sym);
    return v;
}

lval* lval_sexpr(void){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_qexpr(void){
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_func(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;
  v->value.builtin = func;
  return v;
}

lval* lval_lambda(lval* args,lval* body) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUNC;

  v->value.builtin = NULL;
  v->env = lenv_new();
  v->args = args;
  v->body = body;
  return v;
}

lenv* lenv_new(void){

  lenv* env = malloc(sizeof(lenv));
  env->count = 0;
  env->syms = NULL;
  env->vals = NULL;
  env->parent_env = NULL;
  return env;
}

void lval_free(lval* l) {
  switch(l->type){
    case LVAL_NUM: break;
    case LVAL_FUNC:
      if (!l->value.builtin){
        lval_free(l->args);
        lval_free(l->body);
        lenv_free(l->env);
      }
      break;
    case LVAL_ERR:
      free(l->value.err);
      break;
    case LVAL_SYM:
      free(l->value.sym);
      break;
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      for (int i = 0; i< l->count;i++){
        lval_free(l->cell[i]);
      }
      free(l->cell);
  }
  free(l);
}

void lenv_free(lenv* env){

  for(int i = 0; i < env->count; i++){
    free(env->syms[0]);
    lval_free(env->vals[0]);
  }
  free(env->vals);
  free(env->syms);
  free(env);
}

lval* lval_copy(lval* lv){

  lval* copy = (lval*)malloc(sizeof(lval));
  copy->type = lv->type;

  switch (lv->type) {
    case LVAL_NUM: copy->value.num = lv->value.num; break;
    case LVAL_FUNC:
      if (lv->value.builtin){
        copy->value.builtin = lv->value.builtin;
      } else {
        copy->value.builtin = NULL;
        copy->env =  lenv_copy(lv->env);
        copy->args = lval_copy(lv->args);
        copy->body = lval_copy(lv->body);
      }
      break;
    case LVAL_SYM:
       copy->value.sym = malloc(strlen(lv->value.sym) + 1);
       strcpy(copy->value.sym,lv->value.sym);
       break;
    case LVAL_ERR:
       copy->value.err = malloc(strlen(lv->value.err) + 1);
       strcpy(copy->value.err,lv->value.err);
       break;
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      copy->count = lv->count;
      copy->cell = malloc(sizeof(lval*) * copy->count);
      for (int i = 0; i < lv->count; i++){
        copy->cell[i] = lval_copy(lv->cell[i]);
      }
      break;
  }
  return copy;
}
lenv* lenv_copy(lenv* env) {
  lenv* copy = malloc(sizeof(lenv));
  copy->parent_env = env->parent_env;
  copy->count = env->count;
  copy->syms = malloc(sizeof(char*) * copy->count);
  copy->vals = malloc(sizeof(lval*) * copy->count);

  for (int i = 0; i < copy->count; i++) {
    copy->syms[i] = malloc(strlen(env->syms[i]) + 1);
    strcpy(copy->syms[i], env->syms[i]);
    copy->vals[i] = lval_copy(env->vals[i]);
  }
  return copy;
}
lval* env_get(lenv* env, lval* lval_sym){
  char* symbol = lval_sym->value.sym;
  for(int i = 0; i<env->count;i++){
    if(strcmp(symbol,env->syms[i]) == 0) {
      return lval_copy(env->vals[i]);
    }
  }
  if (env->parent_env){
    return env_get(env->parent_env, lval_sym);
  } else {
    return lval_err("Symbol '%s' not bounded", symbol);
  }
}

void env_put(lenv* env, lval* lval_sym, lval* value){

  char* symbol = lval_sym->value.sym;
  for (int i = 0; i<env->count;i++){
    if (strcmp(symbol,env->syms[i]) == 0) {
      lval_free(env->vals[i]);
      env->vals[i] = lval_copy(value);
      return;
    }
  }
  env->count++;

  env->syms = realloc(env->syms, sizeof(char*) * env->count);
  env->vals = realloc(env->vals,sizeof(lval*) * env->count);

  env->vals[env->count-1] = lval_copy(value);
  env->syms[env->count-1] = malloc(strlen(symbol) + 1);
  strcpy(env->syms[env->count - 1], symbol);
}

/* Print an "lval" */
void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM: printf("%li ", v->value.num); break;
    case LVAL_SYM: printf("%s ", v->value.sym); break;
    case LVAL_ERR: printf("%s ", v->value.err); break;
    case LVAL_FUNC:
      if (v->value.builtin) {
        printf("builtin function");
      } else  {
        printf("lambda: ");
        lval_print(v->args);
        putchar(' ');
        lval_print(v->body);
        putchar('\n');
      }
      break;
    case LVAL_SEXPR:
      printf("(");
      for (int i = 0; i < v->count; i++){
        lval_print(v->cell[i]);
      }
      printf(")");
      break;
    case LVAL_QEXPR:
      printf("{");
      for (int i = 0; i < v->count; i++){
        lval_print(v->cell[i]);
      }
      printf("}");
      break;
  }
}

void lval_println(lval* v) {
  lval_print(v);
  putchar('\n');
}

lval* lval_read_num(mpc_ast_t* ast) {
  errno = 0;
  long x = strtol(ast->contents, NULL, 10);
  return errno != ERANGE ?
    lval_num(x) : lval_err("Invalid number %s",ast->contents);
}

lval* lval_add(lval* x, lval* v) {
  x->count++;
  x->cell = realloc(x->cell, sizeof(lval*) * x->count);
  x->cell[x->count-1] = v;
  return x;
}

lval* reader (mpc_ast_t* ast) {
    /* If Symbol or Number return conversion to that type */
  if (strstr(ast->tag, "number")) { return lval_read_num(ast); }
  if (strstr(ast->tag, "symbol")) { return lval_sym(ast->contents); }

  /* If root (>) or sexpr then create empty list */
  lval* x = NULL;
  if (strcmp(ast->tag, ">") == 0 || strstr(ast->tag, "sexpr")) { x = lval_sexpr(); }
  if (strstr(ast->tag, "qexpr")){
    x = lval_qexpr();
  }
   /* Fill this list with any valid expression contained within */
  for (int i = 0; i < ast->children_num; i++) {
    if (strcmp(ast->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(ast->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(ast->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(ast->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(ast->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, reader(ast->children[i]));
  }
  return x;
}

lval* lval_pop (lval* lv, int i) {

  lval* x = lv->cell[i];
  lv->count--;
  /*
   * We want to take all bytes from i+1 possition until the end and put them at i-th position
   * In this way we overwrite content at i-th position and we can shrink the size
  */
  memmove(&lv->cell[i], &lv->cell[i+1], sizeof(lval*) * lv->count-i);
  //decrease memory used
  lv->cell = realloc(lv->cell, sizeof(lval*) * lv->count);
  return x;
}

lval* lval_take (lval* lv, int i) {
  lval* x = lval_pop(lv,i);
  lval_free(lv);
  return x;
}

lval* eval_op(lval* lv, char* op) {

  // pop first arg and accumulate everything in it
  lval* first = lval_pop(lv,0);
  int arg = 1;
  if (first->type != LVAL_NUM){
    lval_free(lv);
    return lval_err("Incorect type passed to %s function for argument 0. Expected %s, got %s",op, ltype_name(LVAL_NUM),ltype_name(first->type));
  }
  if (lv->count == 0 && strcmp(op, "-") == 0) { // unary negation
    first->value.num = -first->value.num;
  }
  while (lv->count > 0) {
    lval* y = lval_pop(lv, 0);
    if (y->type != LVAL_NUM) {
      lval_free(lv);
        return lval_err("Incorect type passed to %s function for argument %d. Expected %s, got %s", op, arg, ltype_name(LVAL_NUM), ltype_name(y->type));
    }
    arg++;
  // should use value.decimal_num if number is decimal
    if (strcmp(op, "+") == 0) { first->value.num += y->value.num; }
    if (strcmp(op, "-") == 0) { first->value.num -= y->value.num; }
    if (strcmp(op, "*") == 0) { first->value.num *= y->value.num;}
    if (strcmp(op, "/") == 0) {
      if (y->value.num == 0){
        lval_free(lv);
        return lval_err("ERROR: Division with 0");
      }
      first->value.num /= y->value.num;
    }
    if (strcmp(op, "%") == 0) { first->value.num %= y->value.num; }
    if (strcmp(op, "^") == 0) {
        if (y->value.num == 0) { return lval_num(1);}
        int res = first->value.num;
        for (int i = 1; i < y->value.num; i++) {
            res*= first->value.num;
        }
        first->value.num = res;
    }
  }
  return first;
}

// builtin methods
lval* builtin_add(lenv* env, lval* lv){
  return eval_op(lv, "+");
}
lval* builtin_sub(lenv* env, lval* lv){
  return eval_op(lv, "-");
}
lval* builtin_div(lenv* env, lval* lv){
  return eval_op(lv, "/");
}
lval* builtin_mult(lenv* env, lval* lv){
  return eval_op(lv, "*");
}
lval* builtin_exp(lenv* env, lval* lv){
  return eval_op(lv, "ˆ");
}
lval* builtin_head(lenv* env, lval* lv){
  LVAL_ASSERT(lv,lv->count==1, "Function 'head' passed to many arguments. Got %d, Expected %d", lv->count,1);
  LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "Function 'head' passed incorect type. Expected %s, got %s", ltype_name(LVAL_QEXPR), ltype_name(lv->cell[0]->type));
  LVAL_ASSERT(lv,lv->cell[0]->count > 0, "Function 'head' passed empty q-expression");

  lval* qexpr = lval_take(lv, 0);
  while (qexpr->count > 1) {
    lval_free(lval_pop(qexpr, 1));
  }
  return qexpr;
}

lval* builtin_tail(lenv* env, lval* lv){
  LVAL_ASSERT(lv,lv->count==1, "function 'tail' passed to many arguments");
  LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "function 'tail' passed incorect type");
  LVAL_ASSERT(lv,lv->cell[0]->count > 0, "function 'tail' passed empty q-expression");

  lval* qexpr = lval_take(lv, 0);
  lval_free(lval_pop(qexpr, 0));
  return qexpr;
}

lval* builtin_list(lenv* env,lval* lv) {
  lv->type = LVAL_QEXPR;
  return lv;
}

lval* builtin_eval(lenv* env, lval* lv){
   LVAL_ASSERT(lv,lv->count==1, "function 'eval' passed to many arguments");
   LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "function 'eval' passed incorect type");

   lval* qexpr = lval_take(lv,0);
   qexpr->type = LVAL_SEXPR; // convert q-expression to s-expression to evaluate
   return lval_eval(env, qexpr);
}

lval* qexpr_join(lval* x, lval* y){

  while(y->count > 0){
    x = lval_add(x, lval_pop(y, 0));
  }
  return x;
}

lval* builtin_join(lenv* env, lval* lv){
  // make sure that each operand is  -eqxpression

  for (int i=0; i< lv->count;i++){
     LVAL_ASSERT(lv,lv->cell[i]->type == LVAL_QEXPR, "function 'join' passed incorect type");
  }

  lval* first = lval_pop(lv, 0);

  while (lv->count > 0){
    first = qexpr_join(first, lval_pop(lv, 0));
  }
  lval_free(lv);
  return first;
}

lval* builtin_cons(lenv* env,lval* lv){

  LVAL_ASSERT(lv,lv->count==2, "function 'cons' must have 2 arguments passed");
  // LVAL_ASSERT(lv,lv->cell[1]->type == LVAL_QEXPR, "function 'cons' must have q-expression as second argument");

  // make new q-expression
  lval* qexpr = lval_qexpr();
  lval_add(qexpr, lval_pop(lv,0));
  lval_add(qexpr, lval_pop(lv,0));
  lval_free(lv);
  return qexpr;
}

lval* builtin_len(lenv* env, lval* lv){

  LVAL_ASSERT(lv,lv->count==1, "function 'len' must have 1 argument passed");
  LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "function 'len' passed incorect type");
  lval* count = lval_num(lv->cell[0]->count);
  lval_free(lv);
  return count;
}

lval* builtin_def(lenv* env, lval* lv){

  // lv is already evaluated here, so we just need to assign it to symbol in env
  LVAL_ASSERT(lv,lv->count==2, "function 'def' must have 2 arguments passed");
  LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "function 'def' passed incorect type");

  lval* symbol = lv->cell[0]->cell[0];
  env_put(env, symbol, lv->cell[1]);
  lval_free(lv);
  return lval_sexpr();
}

lval* builtin_lambda(lenv* env, lval* lv){

  LVAL_ASSERT(lv,lv->count==2, "Function 'lambda' passed wrong number of arguments. Got %d, Expected %d", lv->count,2);
  LVAL_ASSERT(lv,lv->cell[0]->type == LVAL_QEXPR, "Function 'lambda' passed incorect type for argument 0. Expected %s, got %s", ltype_name(LVAL_QEXPR), ltype_name(lv->cell[0]->type));
  LVAL_ASSERT(lv,lv->cell[1]->type == LVAL_QEXPR, "Function 'lambda' passed incorect type for argument 1. Expected %s, got %s", ltype_name(LVAL_QEXPR), ltype_name(lv->cell[1]->type));

  lval* args = lval_pop(lv,0);
  lval* body = lval_pop(lv,0);
  // check that first q expression contains only symbols
  for(int i = 0; i < args->count; i++) {
    LVAL_ASSERT(lv,args->cell[i]->type == LVAL_SYM, "Function arguments must be symbols");
  }

  lval* lambda = lval_lambda(args,body);
  lval_free(lv);
  return lambda;
}

void add_builtin(lenv* env, lval* sym, lval* func){
  env_put(env,sym,func);
  lval_free(sym);
  lval_free(func);
}

void env_add_builtins(lenv* env){
  add_builtin(env, lval_sym("+"), lval_func(builtin_add));
  add_builtin(env, lval_sym("-"), lval_func(builtin_sub));
  add_builtin(env, lval_sym("/"), lval_func(builtin_div));
  add_builtin(env, lval_sym("*"), lval_func(builtin_mult));
  add_builtin(env, lval_sym("^"), lval_func(builtin_exp));

  add_builtin(env, lval_sym("list"), lval_func(builtin_list));
  add_builtin(env, lval_sym("head"), lval_func(builtin_head));
  add_builtin(env, lval_sym("tail"), lval_func(builtin_tail));
  add_builtin(env, lval_sym("join"), lval_func(builtin_join));
  add_builtin(env, lval_sym("eval"), lval_func(builtin_eval));
  add_builtin(env, lval_sym("cons"), lval_func(builtin_cons));
  add_builtin(env, lval_sym("len"), lval_func(builtin_len));
  add_builtin(env, lval_sym("def"), lval_func(builtin_def));
  add_builtin(env, lval_sym("lambda"), lval_func(builtin_lambda));
}

lval* lval_eval_sexpr(lenv* env, lval* v) {

  /* Evaluate Children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(env, v->cell[i]);
  }
   printf("v-cell address: %p \n",&v->cell);
   printf("content of v-cell: %p \n", v->cell);
   printf("value pointed to by v-cell: %p \n", *v->cell);
   printf("address of cell[0]: %p \n", &v->cell[0]);
   printf("content of cell[0]: %p \n", v->cell[0]);
   printf("address of cell[1]: %p \n", &v->cell[1]);
   printf("content of cell[1]: %p \n", v->cell[1]);
   printf("ses %p\n",*(v->cell+1));

  /* Error Checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty Expression */
  if (v->count == 0) { return v; }

  /* Single Expression */
  if (v->count == 1) { return lval_take(v, 0); }

  /* Ensure First Element is Function */
  lval* f = lval_pop(v, 0);

  if (f->type != LVAL_FUNC) {
    lval_free(f);
    lval_free(v);
    return lval_err("S-expression Does not start with function!");
  }

  /* Call builtin with operator */
  lval* result = f->value.builtin(env, v);
  lval_free(f);
  return result;
}

lval* lval_eval(lenv* env, lval* v) {

  /* Evaluate Sexpressions */
  if (v->type == LVAL_SEXPR) {
    return lval_eval_sexpr(env, v);
  }
  /* Symbols should be in env */
  if (v->type == LVAL_SYM) {
    lval* lv = env_get(env,v);
    lval_free(v);
    return lv;
  }
  return v;
}

int main(int argc, char** argv) {
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lisp = mpc_new("lisp");
  mpc_result_t r;
// /-?[0-9]+/ '.' /[0-9]+/ | /-?[0-9]+/;
  mpca_lang(MPCA_LANG_DEFAULT, "                                           \
    number   : /-?[0-9]+([.][0-9]+)?/;                                     \
    symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&^]+/;                          \
    sexpr    : '(' <expr>* ')';                                            \
    qexpr    : '{' <expr>* '}';                                            \
    expr     : <number> | <symbol> | <sexpr> | <qexpr>;                    \
    lisp     : /^/ <expr>* /$/;                                            \
  ", Number, Symbol, Sexpr, Qexpr, Expr, Lisp);

  lenv* env = lenv_new();
  env_add_builtins(env);
  fputs("To exit press ctrl+c\n", stdout);
  while(1) {
    char* input = readline("lispy: ");
    add_history(input);
    if (mpc_parse("<stdin>",input,Lisp,&r)) {
        printf("AST --> \n"); mpc_ast_print(r.output);
        lval* reader_value = reader(r.output);
        printf("INPUT --> "); lval_println(reader_value);

        lval* evaluated = lval_eval(env, reader_value);
        printf("EVALUATED --> "); lval_println(evaluated);
        mpc_ast_delete(r.output);
        // lval_free(reader_value);
        // lval_free(evaluated); already freed in lval_eval
    } else {
        mpc_err_print(r.error);
        mpc_err_delete(r.error);
    }
    free(input);
  }
  lenv_free(env);
// mpc_cleanup(4, Number, Operator, Expr, Lisp);
  return 0;
}
