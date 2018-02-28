#include "nfa.hpp"
#include "baselib.hpp"
#include "word.hpp"
#include "set.hpp"
#include "flags.hpp"

struct phi_w_prefix { // Phis with prefixes
  word *w;
  llist<int> *p;
};

struct inode { // Tree of phis
  char c1;
  char c2;
  struct llist<int> *t;
  struct inode *n1;
  struct inode *n2;
};

struct inode *makeInode(char c1, char c2, struct llist<int> *t, struct inode *n1, struct inode *n2) {
  struct inode *r = new struct inode;
  r->c1 = c1;
  r->c2 = c2;
  r->t = t;
  r->n1 = n1;
  r->n2 = n2;
  return r;
}

struct inode *itr_add(struct inode *tr, char _u, char _v, struct llist<int> *_s) {
  if(tr == NULL)
    return makeInode(_u, _v, _s, NULL, NULL);

  if(_v < tr->c1) {
    tr->n1 = itr_add(tr->n1, _u, _v, _s);
    return tr;
  }
  else if(tr->c2 < _u) {
    tr->n2 = itr_add(tr->n2, _u, _v, _s);
    return tr;
  }
  else {
    struct inode *_ltr, *_rtr;
    if(tr->c1 == _u)
      _ltr = tr->n1;
    else if(tr->c1 < _u)
      _ltr = itr_add(tr->n1, tr->c1, zprev(_u), tr->t);
    else
      _ltr = itr_add(tr->n1, _u, zprev(tr->c1), _s);

    if(tr->c2 == _v)
      _rtr = tr->n2;
    else if(_v < tr->c2)
      _rtr = itr_add(tr->n2, znext(_v), tr->c2, tr->t);
    else
      _rtr = itr_add(tr->n2, znext(tr->c2), _v, _s);

    llist<int> *uni = intset_union(_s, tr->t);
    delete tr->t;
    tr->c1 = max(tr->c1, _u);
    tr->c2 = min(tr->c2, _v);
    tr->t = uni;
    tr->n1 = _ltr;
    tr->n2 = _rtr;
    return tr;
  }
}

struct llist<struct phi_w_prefix *> *itr_collect(struct inode *tr, word *w, struct llist<struct phi_w_prefix *> *lst) {
  if (tr == NULL)
    return lst;
  crange *c = new crange;
  c->a = tr->c1;
  c->b = tr->c2;
  struct phi_w_prefix *phi = new phi_w_prefix;
  phi->w = word_extend(w, c);
  phi->p = tr->t;
  struct llist<struct phi_w_prefix *> *philist = addListNode<struct phi_w_prefix *>(phi, lst);
  return itr_collect(tr->n2, w, itr_collect(tr->n2, w, philist));
}

struct nme {
  char next;
  word *lst;
};

struct nme *nomatch_explore(struct inode *tr, struct nme *nm) {
  if (tr == NULL)
    return nm;
  struct nme *right = nomatch_explore(tr->n2, nm);
  delete nm;
  struct nme *left = new struct nme;
  left->next = tr->c1;
  if (tr->c2 != zprev(right->next)) {
    crange *ct = new crange;
    ct->a = znext(tr->c2);
    ct->b = zprev(right->next);
    left->lst = addListNode<crange *>(ct, right->lst);
  }
  else
    left->lst = right->lst;
  delete right;
  return nomatch_explore(tr->n1, left);
}

word *itr_find_nomatch(struct inode *tr) {
  struct nme *nm = new struct nme;
  nm->next = '\x80';
  nm->lst = NULL;
  nm = nomatch_explore(tr, nm);
  if ('\x00' < nm->next) {
    crange *t = new crange;
    t->a = '\x00';
    t->b = zprev(nm->next);
    nm->lst = addListNode<crange *>(t, nm->lst);
  }
  word *lst = nm->lst;
  delete nm;
  return lst;
}

struct inode *adv_explore_fold_left(struct inode *tr, struct llist<struct transition *> *trans) {
  if (trans == NULL)
    return tr;
  else
    return adv_explore_fold_left(itr_add(tr, trans->head->a, trans->head->b, addListNode<int>(trans->head->c, NULL)), trans->tail);
}

struct inode *adv_explore_intset_fold(struct llist<int> *i, inode *tr, struct nfa *nfa) {
  if (i == NULL)
    return tr;
  else {
    struct llist<struct transition *> *trans = get_transitions(nfa, i->head);
    struct inode *r = adv_explore_intset_fold(i->tail, adv_explore_fold_left(tr, trans), nfa);
    deleteListWithPointers<struct transition *>(trans);
    return r;
  }
}

struct llist<struct phi_w_prefix *> *advance(struct nfa *nfa, word *w, struct llist<int> *p) {
  struct inode *tr = adv_explore_intset_fold(p, NULL, nfa);
  struct llist<struct phi_w_prefix *> *r = itr_collect(tr, w, NULL);
  return r;
}

struct twople <struct llist<struct phi_w_prefix *> *, word *> *explore(struct nfa *nfa, word *w, struct llist<int> *p) {
  struct inode *tr = adv_explore_intset_fold(p, NULL, nfa);
  struct llist<struct phi_w_prefix *> *r1 = itr_collect(tr, w, NULL);
  word *r2 = itr_find_nomatch(tr);
  return makeTwople<struct llist<struct phi_w_prefix *> *, word *>(r1, r2);
}

struct phi_evolve_struct {
  int flgs;
  struct llist<int> *lst;
};

struct phi_evolve_struct *evolve_rec(struct llist<int> *pl, struct llist<int> *st, struct llist<int> *ep, struct nfa *nfa, word *w, int iopt, int flgs) {
  if (pl == NULL) {
    struct phi_evolve_struct *r = new struct phi_evolve_struct;
    r->flgs = flgs;
    r->lst = ep;
    deleteList<int>(st);
    return r;
  }
  else if (listMem<int>(pl->head, st))
    return evolve_rec(pl->tail, st, ep, nfa, w, iopt, flgs); //Already represented in ep, ignore
  st = intset_add(pl->head, st);
  struct state *swt_state = get_state(nfa, pl->head);
  switch(swt_state->type) {
    case End:
      flgs = set_accepting(flgs);
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      return evolve_rec(pl2, st, intset_add(pl->head, ep), nfa, w, iopt, flgs);
    case Kill:
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
    case Pass:
    case MakeB:
    case EvalB:
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      return evolve_rec(addListNode<int>(swt_state->i, pl2), st, ep, nfa, w, iopt, flgs);
    case BeginCap:
    case EndCap:
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      return evolve_rec(addListNode<int>(swt_state->pi->b, pl2), st, ep, nfa, w, iopt, flgs);
    case Match:
      struct llist<int> *pl2 = pl->tail;
      ep = intset_add(pl->head, ep);
      delete pl;
      return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
    case CheckPred:
      if (swt_state->p->type == P_BOI || swt_state->p->type == P_BOL) {
        if (w == NULL || (swt_state->p->type == P_BOL && w->head->a <= '\n' && '\n' <= w->head->b) || (swt_state->p->type == P_BOL && swt_state->p->value == 1 && w->head->a <= '\r' && '\r' <= w->head->b)) {
          struct llist<int> *pl2 = pl->tail;
          delete pl;
          return evolve_rec(addListNode<int>(swt_state->i, pl2), st, ep, nfa, w, iopt, flgs);
        }
        else {
          struct llist<int> *pl2 = pl->tail;
          delete pl;
          return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
        }
      }
      else if (swt_state->p->type == P_EOI) {
        flgs = set_eoihit(flgs);
        struct llist<int> *pl2 = pl->tail;
        ep = intset_add(pl->head, ep);
        delete pl;
        return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
      }
      else {
        flgs = set_interrupted(flgs);
        struct llist<int> *pl2 = pl->tail;
        delete pl;
        return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
      }
    case CheckBackref:
      flgs = set_interrupted(flgs);
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
    case BranchAlt:
      struct llist<int> *pl2 = pl->tail;
      delete pl;
      pl2 = addListNode<int>(swt_state->pi->b, pl2);
      pl2 = addListNode<int>(swt_state->pi->a, pl2);
      return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
    case BranchKln:
      //Is this the kleene we're looking for?
      if (iopt == pl->head)
        flgs = set_klnhit(flgs);
      struct llist<int> *pl2 = addListNode<int>(swt_state->pi->b, pl->tail);
      pl2 = addListNode<int>(swt_state->pi->a, pl2);
      delete pl;
      return evolve_rec(pl2, st, ep, nfa, w, iopt, flgs);
  }
}

struct phi_evolve_struct *phi_evolve(struct nfa *nfa, word *w, struct llist<int> *p, int iopt) {
  int flgs = EMPTY;
  return evolve_rec(listRev<int>(p), NULL, NULL, nfa, w, iopt, flgs);
}

//Check if given character class includes target character
short matches (struct llist<struct pair<char> *> *cls, char c) {
  if (cls == NULL)
    return 0;
  else if (cls->head->a <= c && c <= cls->head->b)
    return 1;
  else
    return matches(cls->tail, c);
}
