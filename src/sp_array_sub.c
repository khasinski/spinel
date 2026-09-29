/* Array subclasses (included into spinel_parse.c).
 *
 *   class Stack < Array
 *     def peek = last
 *   end
 *   s = Stack.new
 *   s << 1 << 2
 *   s.peek        # 2
 *
 * An Array is a builtin value, not a user object with a struct; a class
 * deriving from it has nowhere to keep the elements. Such a class keeps them
 * in an array of its own and forwards Array's methods to it: the superclass
 * is dropped, and on the class line (so the line count holds) it gains
 *
 *   def self.[](*a) = new(a)                        Stack[1, 2]
 *   def initialize(*a, &b) = __spinel_ary_init(*a, &b)   Array.new's forms
 *   def inspect / to_s / to_a / to_ary / ==
 *   def NAME(*a, &b) ... @elements.NAME(a[0], ...) ...
 *
 * one forwarder for each Array method the program calls that the class does
 * not define itself, dispatching on the argument count the program's calls
 * use (a splat into a builtin method takes fixed arguments). A method that
 * answers the receiver itself (<<, push, each with a block, ...) answers the
 * subclass object. `super` in the class's methods calls the held array's
 * method (in initialize: the construction). What the class does not get is
 * Array's identity: `is_a?(Array)`, `Array === x` and passing it where an
 * Array is expected see a plain object. */

static const char *const SAS_ARRAY_NAMES[] = {
#include "array_method_names.inc"
  NULL };

typedef struct { char *p; size_t len, cap; } SasBuf;
static void sas_putn(SasBuf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2 + 64;
    b->p = realloc(b->p, b->cap);
    if (!b->p) { fprintf(stderr, "spinel_parse: out of memory\n"); exit(1); }
  }
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = 0;
}
static void sas_puts(SasBuf *b, const char *s) { sas_putn(b, s, strlen(s)); }

typedef struct { char *name; int maxargc, blk, noblk, splat; } SasUse;
typedef struct { const uint8_t *start, *end; char *text; } SasEdit;

typedef struct {
  SasUse *use; int nuse, cuse;               /* Array method names the program calls */
  pm_class_node_t **cls; int ncls, ccls;      /* the classes deriving from Array */
  SasEdit *ed; int ned, ced;
} SasCtx;

static int sas_is_array_name(const char *n) {
  for (int i = 0; SAS_ARRAY_NAMES[i]; i++) if (strcmp(SAS_ARRAY_NAMES[i], n) == 0) return 1;
  return 0;
}

static int sas_super_is_array(const pm_class_node_t *cn) {
  const pm_node_t *s = cn->superclass;
  if (!s) return 0;
  pm_constant_id_t id;
  if (PM_NODE_TYPE(s) == PM_CONSTANT_READ_NODE) id = ((const pm_constant_read_node_t *)s)->name;
  else if (PM_NODE_TYPE(s) == PM_CONSTANT_PATH_NODE && !((const pm_constant_path_node_t *)s)->parent)
    id = ((const pm_constant_path_node_t *)s)->name;           /* ::Array */
  else return 0;
  char *n = cstr(id);
  int yes = strcmp(n, "Array") == 0;
  free(n);
  return yes;
}

static char *sas_class_name(const pm_class_node_t *cn) {
  const pm_node_t *cp = cn->constant_path;
  if (PM_NODE_TYPE(cp) == PM_CONSTANT_READ_NODE) return cstr(((const pm_constant_read_node_t *)cp)->name);
  if (PM_NODE_TYPE(cp) == PM_CONSTANT_PATH_NODE) return cstr(((const pm_constant_path_node_t *)cp)->name);
  return NULL;
}

static void sas_note_use(SasCtx *x, const char *name, int argc, int blk, int splat) {
  for (int i = 0; i < x->nuse; i++) {
    SasUse *u = &x->use[i];
    if (strcmp(u->name, name) != 0) continue;
    if (argc > u->maxargc) u->maxargc = argc;
    if (blk) u->blk = 1; else u->noblk = 1;
    if (splat) u->splat = 1;
    return;
  }
  if (x->nuse == x->cuse) {
    x->cuse = x->cuse ? x->cuse * 2 : 32;
    x->use = realloc(x->use, sizeof(SasUse) * (size_t)x->cuse);
    if (!x->use) { fprintf(stderr, "spinel_parse: out of memory\n"); exit(1); }
  }
  SasUse *u = &x->use[x->nuse++];
  u->name = strdup(name); u->maxargc = argc; u->blk = blk; u->noblk = !blk; u->splat = splat;
}

static bool sas_collect(const pm_node_t *n, void *data) {
  SasCtx *x = (SasCtx *)data;
  if (PM_NODE_TYPE(n) == PM_CLASS_NODE && sas_super_is_array((const pm_class_node_t *)n)) {
    if (x->ncls == x->ccls) {
      x->ccls = x->ccls ? x->ccls * 2 : 8;
      x->cls = realloc(x->cls, sizeof(*x->cls) * (size_t)x->ccls);
      if (!x->cls) { fprintf(stderr, "spinel_parse: out of memory\n"); exit(1); }
    }
    x->cls[x->ncls++] = (pm_class_node_t *)n;
  }
  if (PM_NODE_TYPE(n) == PM_CALL_NODE) {
    const pm_call_node_t *c = (const pm_call_node_t *)n;
    char *nm = cstr(c->name);
    if (sas_is_array_name(nm)) {
      int argc = 0, splat = 0;
      if (c->arguments)
        for (size_t i = 0; i < c->arguments->arguments.size; i++) {
          if (PM_NODE_TYPE(c->arguments->arguments.nodes[i]) == PM_SPLAT_NODE) splat = 1;
          else argc++;
        }
      sas_note_use(x, nm, argc, c->block != NULL, splat);
    }
    /* to_enum(:each) / enum_for(:each): the enumerator runs the named method
       with a block */
    else if ((strcmp(nm, "to_enum") == 0 || strcmp(nm, "enum_for") == 0) && c->arguments &&
             c->arguments->arguments.size >= 1 &&
             PM_NODE_TYPE(c->arguments->arguments.nodes[0]) == PM_SYMBOL_NODE) {
      const pm_symbol_node_t *sy = (const pm_symbol_node_t *)c->arguments->arguments.nodes[0];
      size_t l = pm_string_length(&sy->unescaped);
      char tn[128];
      if (l < sizeof tn) {
        memcpy(tn, pm_string_source(&sy->unescaped), l); tn[l] = 0;
        if (sas_is_array_name(tn)) sas_note_use(x, tn, (int)c->arguments->arguments.size - 1, 1, 0);
      }
    }
    free(nm);
  }
  return true;
}

/* The names the class's bodies define themselves (any body of that name). */
typedef struct { const char *cname; char **names; int n, cap; int self_index; } SasDefs;
static void sas_add_def(SasDefs *d, const char *n) {
  if (d->n == d->cap) {
    d->cap = d->cap ? d->cap * 2 : 16;
    d->names = realloc(d->names, sizeof(char *) * (size_t)d->cap);
    if (!d->names) { fprintf(stderr, "spinel_parse: out of memory\n"); exit(1); }
  }
  d->names[d->n++] = strdup(n);
}
static bool sas_defs_visit(const pm_node_t *n, void *data) {
  SasDefs *d = (SasDefs *)data;
  if (PM_NODE_TYPE(n) != PM_CLASS_NODE) return true;
  const pm_class_node_t *cn = (const pm_class_node_t *)n;
  char *nm = sas_class_name(cn);
  int same = nm && strcmp(nm, d->cname) == 0;
  free(nm);
  if (!same || !cn->body || PM_NODE_TYPE(cn->body) != PM_STATEMENTS_NODE) return true;
  const pm_statements_node_t *st = (const pm_statements_node_t *)cn->body;
  for (size_t i = 0; i < st->body.size; i++) {
    const pm_node_t *s = st->body.nodes[i];
    if (PM_NODE_TYPE(s) == PM_DEF_NODE) {
      const pm_def_node_t *df = (const pm_def_node_t *)s;
      char *dn = cstr(df->name);
      if (!df->receiver) sas_add_def(d, dn);
      else if (strcmp(dn, "[]") == 0) d->self_index = 1;
      free(dn);
    }
    else if (PM_NODE_TYPE(s) == PM_CALL_NODE) {
      const pm_call_node_t *c = (const pm_call_node_t *)s;
      char *cn2 = cstr(c->name);
      int attr = !c->receiver && (strcmp(cn2, "attr_reader") == 0 || strcmp(cn2, "attr_accessor") == 0 ||
                                   strcmp(cn2, "attr_writer") == 0 || strcmp(cn2, "alias_method") == 0);
      if (attr && c->arguments)
        for (size_t a = 0; a < c->arguments->arguments.size; a++) {
          const pm_node_t *an = c->arguments->arguments.nodes[a];
          if (PM_NODE_TYPE(an) != PM_SYMBOL_NODE) continue;
          const pm_symbol_node_t *sy = (const pm_symbol_node_t *)an;
          char buf[256];
          size_t l = pm_string_length(&sy->unescaped);
          if (l >= sizeof buf - 1) continue;
          memcpy(buf, pm_string_source(&sy->unescaped), l); buf[l] = 0;
          if (strcmp(cn2, "attr_writer") == 0 || strcmp(cn2, "attr_accessor") == 0) {
            char w[258]; snprintf(w, sizeof w, "%s=", buf); sas_add_def(d, w);
          }
          if (strcmp(cn2, "attr_writer") != 0) sas_add_def(d, buf);
          if (strcmp(cn2, "alias_method") == 0) break;       /* only the new name */
        }
      free(cn2);
    }
    else if (PM_NODE_TYPE(s) == PM_ALIAS_METHOD_NODE) {
      const pm_node_t *nn = ((const pm_alias_method_node_t *)s)->new_name;
      if (PM_NODE_TYPE(nn) == PM_SYMBOL_NODE) {
        const pm_symbol_node_t *sy = (const pm_symbol_node_t *)nn;
        char buf[256];
        size_t l = pm_string_length(&sy->unescaped);
        if (l < sizeof buf - 1) { memcpy(buf, pm_string_source(&sy->unescaped), l); buf[l] = 0; sas_add_def(d, buf); }
      }
    }
  }
  return true;
}
static int sas_defined(const SasDefs *d, const char *n) {
  for (int i = 0; i < d->n; i++) if (strcmp(d->names[i], n) == 0) return 1;
  return 0;
}

/* Array methods that answer the receiver itself: the subclass object is what
   they answer here. `each`-like ones do only with a block (without one they
   answer an Enumerator); the bang filters answer nil when nothing changed. */
static int sas_returns_self(const char *n) {
  static const char *const S[] = { "<<", "push", "append", "unshift", "prepend", "insert", "concat",
    "clear", "replace", "fill", NULL };
  for (int i = 0; S[i]; i++) if (strcmp(S[i], n) == 0) return 1;
  return 0;
}
static int sas_returns_self_with_block(const char *n) {
  static const char *const S[] = { "each", "each_index", "reverse_each", "map!", "collect!",
    "keep_if", "delete_if", "sort!", "sort_by!", "rotate!", "shuffle!", "reverse!", NULL };
  for (int i = 0; S[i]; i++) if (strcmp(S[i], n) == 0) return 1;
  return 0;
}
static int sas_returns_self_or_nil(const char *n) {
  static const char *const S[] = { "select!", "filter!", "reject!", "uniq!", "compact!", "flatten!", NULL };
  for (int i = 0; S[i]; i++) if (strcmp(S[i], n) == 0) return 1;
  return 0;
}

/* `__spinel_aryv.NAME(a[0], ..., a[k-1][, &b])` */
/* k < 0: the arguments splatted on, however many they are */
static void sas_call(SasBuf *b, const char *name, int k, int blk) {
  sas_puts(b, "__spinel_aryv.");
  sas_puts(b, name);
  sas_puts(b, "(");
  if (k < 0) { sas_puts(b, "*spinel_aa"); k = 1; }
  else for (int i = 0; i < k; i++) {
    char t[32]; snprintf(t, sizeof t, "%sspinel_aa[%d]", i ? ", " : "", i);
    sas_puts(b, t);
  }
  if (blk) sas_puts(b, k ? ", &spinel_ab" : "&spinel_ab");
  sas_puts(b, ")");
}

/* one argument count's arm: the call, wrapped by what the method answers */
static void sas_arm(SasBuf *b, const SasUse *u, int k) {
  int both = u->blk && u->noblk;
  if (both) sas_puts(b, "(spinel_ab ? ");
  if (u->blk) {
    sas_puts(b, "(");
    if (sas_returns_self(u->name) || sas_returns_self_with_block(u->name)) {
      sas_call(b, u->name, k, 1); sas_puts(b, "; self");
    }
    else if (sas_returns_self_or_nil(u->name)) {
      sas_puts(b, "("); sas_call(b, u->name, k, 1); sas_puts(b, ").nil? ? nil : self");
    }
    else sas_call(b, u->name, k, 1);
    sas_puts(b, ")");
  }
  if (both) sas_puts(b, " : ");
  if (u->noblk) {
    sas_puts(b, "(");
    if (sas_returns_self(u->name)) { sas_call(b, u->name, k, 0); sas_puts(b, "; self"); }
    else if (sas_returns_self_or_nil(u->name)) {
      sas_puts(b, "("); sas_call(b, u->name, k, 0); sas_puts(b, ").nil? ? nil : self");
    }
    else sas_call(b, u->name, k, 0);
    sas_puts(b, ")");
  }
  if (both) sas_puts(b, ")");
}

/* A regular def (an endless one cannot define a setter such as `[]=`):
   one arm per argument count the program's calls use, and a call through a
   splat passes the arguments on as they come. */
static void sas_forwarder(SasBuf *b, const SasUse *u) {
  sas_puts(b, "; def ");
  sas_puts(b, u->name);
  sas_puts(b, "(*spinel_aa, &spinel_ab); ");
  if (u->maxargc == 0 && !u->splat) {    /* only ever called without arguments */
    sas_arm(b, u, 0);
    sas_puts(b, "; end");
    return;
  }
  sas_puts(b, "case spinel_aa.size");
  for (int k = 0; k <= u->maxargc; k++) {
    char t[32]; snprintf(t, sizeof t, "; when %d then ", k);
    sas_puts(b, t);
    sas_arm(b, u, k);
  }
  sas_puts(b, "; else ");
  if (u->splat) sas_arm(b, u, -1);
  else sas_puts(b, "raise ArgumentError, \"wrong number of arguments\"");
  sas_puts(b, "; end; end");
}

/* `super` in the class's methods: the held array's method, or in
   initialize the construction */
typedef struct { SasCtx *x; const char *mname; const pm_def_node_t *def; int failed; } SasSuperWalk;

static int sas_params_text(const pm_def_node_t *df, SasBuf *b) {
  const pm_parameters_node_t *ps = df->parameters;
  if (!ps) return 1;
  if (ps->keywords.size || ps->keyword_rest || ps->posts.size) return 0;
  int first = 1;
  for (size_t i = 0; i < ps->requireds.size; i++) {
    const pm_node_t *r = ps->requireds.nodes[i];
    if (PM_NODE_TYPE(r) != PM_REQUIRED_PARAMETER_NODE) return 0;
    char *n = cstr(((const pm_required_parameter_node_t *)r)->name);
    if (!first) sas_puts(b, ", ");
    sas_puts(b, n); free(n); first = 0;
  }
  for (size_t i = 0; i < ps->optionals.size; i++) {
    char *n = cstr(((const pm_optional_parameter_node_t *)ps->optionals.nodes[i])->name);
    if (!first) sas_puts(b, ", ");
    sas_puts(b, n); free(n); first = 0;
  }
  if (ps->rest && PM_NODE_TYPE(ps->rest) == PM_REST_PARAMETER_NODE) {
    pm_constant_id_t id = ((const pm_rest_parameter_node_t *)ps->rest)->name;
    if (!id) return 0;
    char *n = cstr(id);
    if (!first) sas_puts(b, ", ");
    sas_puts(b, "*"); sas_puts(b, n); free(n); first = 0;
  }
  if (ps->block) {
    pm_constant_id_t id = ((const pm_block_parameter_node_t *)ps->block)->name;
    if (!id) return 0;
    char *n = cstr(id);
    if (!first) sas_puts(b, ", ");
    sas_puts(b, "&"); sas_puts(b, n); free(n);
  }
  return 1;
}

static void sas_add_edit(SasCtx *x, const uint8_t *s, const uint8_t *e, char *text) {
  if (x->ned == x->ced) {
    x->ced = x->ced ? x->ced * 2 : 16;
    x->ed = realloc(x->ed, sizeof(SasEdit) * (size_t)x->ced);
    if (!x->ed) { fprintf(stderr, "spinel_parse: out of memory\n"); exit(1); }
  }
  x->ed[x->ned].start = s; x->ed[x->ned].end = e; x->ed[x->ned].text = text; x->ned++;
}

static bool sas_super_visit(const pm_node_t *n, void *data) {
  SasSuperWalk *w = (SasSuperWalk *)data;
  pm_node_type_t t = PM_NODE_TYPE(n);
  if (t == PM_DEF_NODE || t == PM_CLASS_NODE || t == PM_MODULE_NODE || t == PM_SINGLETON_CLASS_NODE)
    return false;                                      /* another method's super */
  if (t != PM_SUPER_NODE && t != PM_FORWARDING_SUPER_NODE) return true;
  SasBuf b = {0};
  int init = strcmp(w->mname, "initialize") == 0;
  if (init) sas_puts(&b, "__spinel_ary_init(");
  else { sas_puts(&b, "__spinel_aryv."); sas_puts(&b, w->mname); sas_puts(&b, "("); }
  const pm_node_t *blk = NULL;
  if (t == PM_SUPER_NODE) {
    const pm_super_node_t *sn = (const pm_super_node_t *)n;
    if (sn->arguments)
      sas_putn(&b, (const char *)sn->arguments->base.location.start,
               (size_t)(sn->arguments->base.location.end - sn->arguments->base.location.start));
    blk = sn->block;
    if (blk && PM_NODE_TYPE(blk) == PM_BLOCK_ARGUMENT_NODE) {
      if (sn->arguments) sas_puts(&b, ", ");
      sas_putn(&b, (const char *)blk->location.start, (size_t)(blk->location.end - blk->location.start));
      blk = NULL;
    }
  }
  else {
    if (!sas_params_text(w->def, &b)) { free(b.p); w->failed = 1; return false; }
    blk = (const pm_node_t *)((const pm_forwarding_super_node_t *)n)->block;
  }
  sas_puts(&b, ")");
  if (blk) {                                           /* a literal block stays on the call */
    sas_puts(&b, " ");
    sas_putn(&b, (const char *)blk->location.start, (size_t)(blk->location.end - blk->location.start));
  }
  if (!init && sas_returns_self(w->mname)) {
    SasBuf p = {0};
    sas_puts(&p, "("); sas_puts(&p, b.p); sas_puts(&p, "; self)");
    free(b.p); b = p;
  }
  sas_add_edit(w->x, n->location.start, n->location.end, b.p);
  return false;
}

static int sas_edit_cmp(const void *a, const void *b) {
  const SasEdit *x = a, *y = b;
  return x->start < y->start ? -1 : x->start > y->start;
}

/* cheap gate: `< Array` / `< ::Array` somewhere */
static int sas_mentions_array_super(const char *src) {
  for (const char *p = strchr(src, '<'); p; p = strchr(p + 1, '<')) {
    const char *q = p + 1;
    while (*q == ' ' || *q == '\t') q++;
    if (q[0] == ':' && q[1] == ':') q += 2;
    if (strncmp(q, "Array", 5) == 0 && !sp_req_ident_char(q[5])) return 1;
  }
  return 0;
}

static char *sp_expand_array_subclasses(const char *source) {
  if (!sas_mentions_array_super(source)) return NULL;
  size_t len = strlen(source);
  pm_parser_t parser;
  pm_parser_init(&parser, (const uint8_t *)source, len, NULL);
  pm_node_t *root = pm_parse(&parser);
  const pm_parser_t *sv = g_parser;
  g_parser = &parser;
  char *result = NULL;
  SasCtx x = {0};
  if (parser.error_list.size == 0) {
    pm_visit_node(root, sas_collect, &x);
    for (int i = 0; i < x.ncls; i++) {
      pm_class_node_t *cn = x.cls[i];
      char *cname = sas_class_name(cn);
      if (!cname) continue;
      SasDefs d = {0}; d.cname = cname;
      pm_visit_node(root, sas_defs_visit, &d);
      SasBuf b = {0};
      if (!d.self_index) sas_puts(&b, "; def self.[](*spinel_aa) = new(spinel_aa)");
      sas_puts(&b, "; def __spinel_ary_init(*spinel_aa, &spinel_ab) = @__spinel_ary = (case spinel_aa.size"
                   "; when 0 then []"
                   "; when 1 then (spinel_aa[0].is_a?(Array) ? spinel_aa[0].dup :"
                   " (spinel_ab ? Array.new(spinel_aa[0]) { |spinel_i| spinel_ab.call(spinel_i) } : Array.new(spinel_aa[0])))"
                   "; else Array.new(spinel_aa[0], spinel_aa[1]); end)");
      sas_puts(&b, "; def __spinel_aryv = (@__spinel_ary ||= [])");
      if (!sas_defined(&d, "initialize"))
        sas_puts(&b, "; def initialize(*spinel_aa, &spinel_ab) = __spinel_ary_init(*spinel_aa, &spinel_ab)");
      static const char *const fixed[][2] = {
        { "to_a", "__spinel_aryv" }, { "to_ary", "__spinel_aryv" }, { "entries", "__spinel_aryv" },
        { "inspect", "__spinel_aryv.inspect" }, { "to_s", "__spinel_aryv.inspect" }, { NULL, NULL } };
      for (int f = 0; fixed[f][0]; f++) {
        if (sas_defined(&d, fixed[f][0])) continue;
        sas_puts(&b, "; def "); sas_puts(&b, fixed[f][0]); sas_puts(&b, " = "); sas_puts(&b, fixed[f][1]);
      }
      if (!sas_defined(&d, "eql?")) {
        sas_puts(&b, "; def eql?(spinel_o) = __spinel_aryv.eql?(spinel_o.is_a?(");
        sas_puts(&b, cname);
        sas_puts(&b, ") ? spinel_o.to_a : spinel_o)");
      }
      if (!sas_defined(&d, "==")) {
        sas_puts(&b, "; def ==(spinel_o) = __spinel_aryv == (spinel_o.is_a?(");
        sas_puts(&b, cname);
        sas_puts(&b, ") ? spinel_o.to_a : spinel_o)");
      }
      for (int u = 0; u < x.nuse; u++) {
        const char *un = x.use[u].name;
        if (sas_defined(&d, un) || strcmp(un, "==") == 0 || strcmp(un, "inspect") == 0 ||
            strcmp(un, "to_s") == 0 || strcmp(un, "to_a") == 0 || strcmp(un, "to_ary") == 0 ||
            strcmp(un, "entries") == 0 || strcmp(un, "hash") == 0 || strcmp(un, "eql?") == 0 ||
            strcmp(un, "initialize") == 0)
          continue;
        sas_forwarder(&b, &x.use[u]);
      }
      /* super in the class's own methods; a bare super whose parameters
         cannot be spelled again (keywords) leaves the whole class as it was */
      int mark = x.ned, failed = 0;
      if (cn->body && PM_NODE_TYPE(cn->body) == PM_STATEMENTS_NODE) {
        const pm_statements_node_t *st = (const pm_statements_node_t *)cn->body;
        for (size_t s = 0; s < st->body.size && !failed; s++) {
          const pm_node_t *sn = st->body.nodes[s];
          if (PM_NODE_TYPE(sn) != PM_DEF_NODE || ((const pm_def_node_t *)sn)->receiver) continue;
          const pm_def_node_t *df = (const pm_def_node_t *)sn;
          char *mn = cstr(df->name);
          SasSuperWalk w = { &x, mn, df, 0 };
          if (df->body) pm_visit_node(df->body, sas_super_visit, &w);
          failed = w.failed;
          free(mn);
        }
      }
      if (failed) {
        while (x.ned > mark) free(x.ed[--x.ned].text);
        free(b.p);
      }
      else {
        /* `class Stack < Array` -> `class Stack; <the definitions>` */
        const uint8_t *hs = cn->constant_path->location.end, *he = cn->superclass->location.end;
        sas_add_edit(&x, hs, he, b.p);
      }
      for (int q = 0; q < d.n; q++) free(d.names[q]);
      free(d.names);
      free(cname);
    }
    if (x.ned > 0) {
      qsort(x.ed, (size_t)x.ned, sizeof(SasEdit), sas_edit_cmp);
      SasBuf nb = {0};
      const uint8_t *from = (const uint8_t *)source;
      for (int i = 0; i < x.ned; i++) {
        if (x.ed[i].start < from) continue;
        sas_putn(&nb, (const char *)from, (size_t)(x.ed[i].start - from));
        sas_puts(&nb, x.ed[i].text);
        from = x.ed[i].end;
      }
      sas_putn(&nb, (const char *)from, (size_t)((const uint8_t *)source + len - from));
      result = nb.p;
    }
  }
  for (int i = 0; i < x.ned; i++) free(x.ed[i].text);
  free(x.ed);
  for (int i = 0; i < x.nuse; i++) free(x.use[i].name);
  free(x.use);
  free(x.cls);
  g_parser = sv;
  pm_node_destroy(&parser, root);
  pm_parser_free(&parser);
  return result;
}
