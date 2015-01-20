/**
 *  Description of a lock-free protocol for Mounds.  Note that this is a
 *  transactional protocol, but it is not a general-purpose TM.
 *
 *  In particular, we do not have deadlock detection, per se.  We rely on
 *  mound properties (strict order of accesses) for that purpose.
 *
 *  One funny thing here is that if a DCAS fails, we re-set timestamps to their
 *  old values.  So there is a logical level and a concrete level, and
 *  timestamp changes differ at the two levels.
 *
 *  For now, all helping will be in READ.
 */

/**
 * Nodes in the Mound consist of a pointer to a list, a flag indicating if
 * the list is in an unusable state, and a timestamp.
 */
struct Node
{
  private:
    volatile List* list   = null;
    // pointer to head node
    volatile bool  cavity = false;
    // indicate whether the node is a cavity
    volatile int   ts     = 0;
    // timestamp... useful for breaking up
    // atomic sections
  public:
    nodecache_t READ();
    // returns a nodecache
};

/**
 *  The following are the atomic operations we perform, apart from READ.
 */

// A (ATOMIC_CAS(C, CC, (new List(n, CC->list), false, CC.ts+1)))
//   (that's insert equal value)

// B (ATOMIC_C2S1(C, CC, (new List(n, CC->list), false, CC.ts+1), P, PP, NO_CHANGE))
//   (that's insert at child with parent constant)

// C (ATOMIC_CAS(P, PP, (new List(n, PP.list), false, PP.ts+1)))
//   (that's insert at root)

// D (ATOMIC_C2S1(N, NN, (NN.list, false, NN.ts + 1), LEAFINFO, LEAFINFO, NO_CHANGE))
//   (that's clear leaf cavity)

// E (ATOMIC_C2S2(N, NN, (RR.list, false, NN.ts + 1), R, RR, (NN.list, true,  RR.ts + 1)))
//   (that's move cavity right)

// F (ATOMIC_C2S2(N, NN, (LL.list, false, NN.ts + 1), L, LL, (NN.list, true,  LL.ts + 1)))
//   (that's move cavity left)

// G (ATOMIC_CAS(N, NN, (NN.list, false, NN.ts + 1)))
//   (that's fix cavity from local list)

// H (ATOMIC_CAS(ROOT, RR, (extracted->next, true, RR.ts + 1)))
//   (extract min and make root cavity)

/**
 *  There are two that are missing: we should have two ATOMIC_CAS operations
 *  that use LEAFINFO, in order to enlarge the mound.  The first should only
 *  CAS NULL pointers to non-NULL.  The second should use CAS to try to
 *  increase the value of an integer.  The second one will look like this:
 */

// I (ATOMIC_CAS(LEAFINFO, LI, LI+1))

/**
 *  The first step is to augment Node.  We have a spare bit in the pointer,
 *  so this is OK
 */
struct LFNode
{
    volatile List* list   = null;
    // pointer to head node
    volatile bool  cavity = false;
    // indicate whether the node is a cavity
    volatile bool  owned  = false;
    // indicate if we should use ts or owner
    union
    {
        volatile int   ts     = 0;
        // timestamp... useful for breaking up
        volatile Owner* owner = NULL;
    };
};

/**
 *  CAS I is easy, because it works on integers that are
 *  monotonically increasing.  It can even be 32-bit.
 */

/**
 *  CAS A/C/G/H operate on a single Node.  They must be 64-bit, and the
 *  challenge is to make sure they compose correctly.
 */

/**
 *  C2S1 B Keeps one Node constant while changing another.
 *  C2S1 D Keeps an integer constant while changing a Node
 */

/**
 *  C2S2 E/F perform a DCAS on two nodes
 */

// [mfs] Given that C2S1 B changes child and keeps parent const, I think we
//       need to make DCAS acquire child before parent.

/**
 *  We cannot ignore that Node->READ must be implemented correctly
 */

/**
 *  Proposal: We will begin with C2S2, which should be the hardest, and then
 *  work backwards from there.
 *
 *  Trick: we will never abort an in-flight op 'just for fun'.  We will only
 *         'let one fail' when it cannot succeed.
 *
 *  The C2S2 has the property that all values (old and new) must be known
 *  before the call is initated.  They also always operate on a parent and
 *  child.
 */

struct Owner
{
    Node* a;
    nodecache_t a_old;
    nodecache_t a_new;

    Node* b;
    nodecache_t b_old;
    nodecache_t b_new;

    int* i;
    int i_old;

    volatile enum
    {
        TRY_C2S2, FAIL_C2S2, OK_C2S2,
        TRY_C2S1, FAIL_C2S1, OK_C2S1,
        TRY_C2S1I, FAIL_C2S1I, OK_C2S1I
    } status;
};

bool C2S2(Node* a, nodecache_t a_old, nodecache_t a_new,
          Node* b, nodecache_t b_old, nodecache_t b_new)
{
    // in reality, we will use version #s and recycle these...  Probably need
    // to use a seqlock for the versioning.
    LFNode* o = new LFNode(list=NULL,
                           cavity=false,
                           owned=true,
                           owner=(a, a_old, a_new, b, b_old, b_new, TRY_C2S2));

    // op is invisible until I install A
    if (!CAS(a, a_old, o)) {
        // TODO: we could help here before returning false
        return false;
    }

    // At the point where I installed A, someone could do B for me.
    if (!CAS(b, b_old, o)) {
        // someone could have done **a lot** for me.  Check the enum.
        if (o->status == OK_C2S2)
            goto cleanup;

        // someone could have done **a little* for me.  Check b
        if (*b == o) {
            o->status = OK_C2S2;
            goto cleanup;
        }

        // nobody helped me :(

        // TODO: the lock-free guarantee is very weak here.  We might want to
        // check if b is owned, and help.  Worst case is that lots of ops on
        // overlapping parent/child pairs will all fail, except for the one
        // whose child is a leaf.  That's still lock-free, but weakly so.  If
        // we helped, and the help led to someone else failing, we could then
        // clean up and succeed in our own operation.
        o->status = FAIL_C2S2;

        // clean up a
        CAS(a, o, a_old);
    }
    else {
        o->status = OK_C2S2;
    }

  cleanup:
    // once the second CAS succeeds, the C2S2 is done.  The status is OK_C2S2
    // by the time we're here, so anyone can help clean up.  That means we need
    // CASes.  Cleanup order does not matter.  May want to use test-and-CAS?
    CAS(a, o, a_new);
    CAS(b, o, b_new);
}


/**
 *  Now let's try the C2S1 that operates on two nodes
 */
bool C2S1(Node* a, nodecache_t a_old, nodecache_t a_new,
          Node* b, nodecache_t b_old)
{
    LFNode o = new LFNode(list=NULL,
                          cavity=false,
                          owned=true,
                          owner=(a, a_old, a_new, b, b_old, TRY_C2S1));

    // op is invisible until I install A
    if (!CAS(a, a_old, o)) {
        // TODO: we could help here before returning false
        return false;
    }

    // if nobody is helping out, then I should just check B and clean A
    if (*b == b_old) {

        // linearize at the point where we clean A
        if (CAS(a, o, a_new))
            return true;

        // if the CAS failed, then there are two possibilities.  The first is
        // that someone helped me succeed.  That means they checked B, CASed me
        // to OK, and are trying to clean A.
        if (o->status == OK_C2S1) {
            // i'm good... no need to clean up
            return true;
        }

        // the second case is that someone changed B, oblivious to my
        // existance, right after I checked B.  Then someone else tried to help
        // and saw the old B, and declared that I failed.
        if (o->status == FAIL_C2S1) {
            // again, no need to clean up... but this is really unfortunate :(
            return false;
        }
    }

    // if that check failed, I might still be OK.  If someone saw A, and then
    // did the work for me, I'm still good.
    if (o->status == OK_C2S1) {
        CAS(a, o, a_new);
        // test and CAS?
        return true;
    }

    // the only other possibilities are that o->status is TRY_C2S1 and that
    // o->status is FAIL_C2S1.  in the former case, nobody tried to help me,
    // but someone changed B.  In the latter case, someone tried to help me,
    // but it was after a change to B.

    // might I need to help B's owner, in the event that B fails and I can
    // survive?  That might be vital to lock-freedom, since this CASes a child
    // and compares a parent.  But it would also require me to release A, then
    // clean B.
    //
    // [mfs] Such actions would be MUCH simpler if the ts field didn't get
    //       destroyed when I installed an owner...

    // I failed.  clean up
    CAS(a, o, a_old);
    // test and CAS?
}

/**
 *  Now let's try the C2S1 that operates on a node and an int
 */
bool C2S1I(Node* a, nodecache_t a_old, nodecache_t a_new,
           int* i,  int i_old)
{
    LFNode o = new LFNode(list=NULL,
                          cavity=false,
                          owned=true,
                          owner=(a, a_old, a_new, i, i_old, TRY_C2S1I));

    // op is invisible until I install A
    if (!CAS(a, a_old, o)) {
        // TODO: we could help here before returning false
        return false;
    }

    // if nobody is helping out, then I should just check B and clean A
    if (*i == i_old) {
        // linearize at the point where we clean A
        if (CAS(a, o, a_new))
            return true;

        // if the CAS failed, then there are two possibilities.  The first is
        // that someone helped me succeed.  That means they checked B, CASed me
        // to OK, and are trying to clean A.
        if (o->status == OK_C2S1I) {
            // i'm good... no need to clean up
            return true;
        }

        // the second case is that someone changed B, oblivious to my
        // existance, right after I checked B.  Then someone else tried to help
        // and saw the old B, and declared that I failed.
        if (o->status == FAIL_C2S1I) {
            // again, no need to clean up... but this is really unfortunate :(
            return false;
        }
    }

    // if that check failed, I might still be OK.  If someone saw A, and then
    // did the work for me, I'm still good.
    if (o->status == OK_C2S1I) {
        CAS(a, o, a_new); // test and CAS?
        return true;
    }

    CAS(a, o, a_old); // test and CAS?
}

/**
 *  This is where all the helping happens
 */
nodecache_t READ(Node* N)
{
    while (true) {
        // atomic read of the node
        nodecache_t v = *N;

        // common case: v is not owned
        if (!v.owned)
            return v;

        // ick.  I have to help.  First step: snapshot the owner
        Owner o = *v.owner;

        // double check that owner still installed, else snapshot invalid
        if (*N != v)
            continue;

        // [mfs] I don't trust the snapshotting.  I think we need a counter in
        // the Owner object.  The reason is that I could install an Owner,
        // abort, clear back, re-install the same owner, ...
        //
        // Note that the above problem could be avoided if ts always increased
        // on any CAS.
        //
        // Note that another solution is to have each thread use a counter that
        // increments on every acquisition, and use that as the other half of
        // the nodecache_t installed when acquiring.  That's cleanest.

        // [mfs] stopped here

        // time to decipher the operation
        if (o.status == TRY_C2S2) {
            // if we are here, then there are two possibilities:

            // I read the first location that was acquired.
            //
            //   - if second location already acquired, help with cleanup.
            //   - No need to change owner status
            //
            //   - else help acquire second location, cas owner status, and
            //     then help with cleanup.
            //     Note that there needs to be a counter with the owner
            //     status, or else we have ABA problems

            // I read the second location that was acquired.  In this case,
            // the operation is logically complete, so I should help clean
            // up.
            //
            // NB: helping clean up requires counted owner pointers, or else
            //     I could clean up the wrong thing.  We can't just install
            //     "owner" into the Node*.
        }
        else if (o.status == FAIL_C2S2) {
            // help clean up.  Do this with two test-and-CASes (TACAS)

        }
        else if (o.status == OK_C2S2) {
            // someone already is helping, now we are too.  Use two TACASes

        }
        else if (o.status == TRY_C2S1) {
            // swap field is acquired, but we don't know if the compare field
            // is OK.

            // do the compare.  If it fails, if CAS status to FAIL_C2S1 then
            // try to uninstall the owner.  else we could be smart, or just
            // continue.  In fact, almost every cleanup CAS should be a
            // bool_cas, so that we can just continue any time any CAS fails.
            //
            // If the compare succeeds, then CAS status to OK_C2S1.  If
            // fails, try to cleanup.  If succeeds, try to update.

        }
        else if (o.status == FAIL_C2S1) {
            // help cleanup swap field to failure

        }
        else if (o.status == OK_C2S1) {
            // help cleanup swap field to success

        }
        else if (o.status == TRY_C2S1I) {
            // no longer needed

        }
        else if (o.status == FAIL_C2S1I) {
            // no longer needed

        }
        else if (o.status == OK_C2S1I) {
            // no longer needed

        }
    }
}
