#include "scope.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <unordered_map>

struct Small { int32_t v; };
struct Big { double a, b, c, d; };

int destructedSmall = 0;
struct TrackedSmall {
    int32_t v;
    explicit TrackedSmall(int32_t v_) : v(v_) {}
    TrackedSmall(TrackedSmall&& o) noexcept : v(o.v) {}
    ~TrackedSmall() { destructedSmall++; }
};

template <typename Fn>
bool throws(Fn&& fn) {
    try {
        fn();
        return false;
    } catch (...) {
        return true;
    }
}

int main() {
    // --- 1. Basic push/access ---
    {
        Scope s;
        Handle<Small> h1 = s.push<Small>(Small{42});
        Handle<Small> h2 = s.push<Small>(Small{7});
        assert(h1.get()->v == 42);
        h2->v = 99;
        assert(h2->v == 99);
        printf("[ok] basic push/access\n");
    }

    // --- 2. exit() destroys everything not moved out, immediately ---
    {
        Scope s;
        destructedSmall = 0;
        s.push<TrackedSmall>(1);
        s.push<TrackedSmall>(2);
        s.exit();
        assert(destructedSmall == 2);
        printf("[ok] exit() destroys unmoved objects immediately\n");
    }

    // --- 3. Falling out of scope without exit() still cleans up ---
    {
        destructedSmall = 0;
        {
            Scope s;
            s.push<TrackedSmall>(1);
            s.push<TrackedSmall>(2);
        }
        assert(destructedSmall == 2);
        printf("[ok] destructor still cleans up if exit() was never called\n");
    }

    // --- 4. get() via an old handle after exit() throws cleanly ---
    {
        Scope s;
        Handle<Small> h = s.push<Small>(Small{1});
        s.exit();
        assert(throws([&] { h.get(); }));
        printf("[ok] get() after exit() throws (via poisoned pool, no lookup involved)\n");
    }

    // --- 5. Push of an already-used type after exit() throws ---
    {
        Scope s;
        s.push<Small>(Small{1});
        s.exit();
        assert(throws([&] { s.push<Small>(Small{2}); }));
        printf("[ok] push of a previously-used type after exit() throws\n");
    }

    // --- 6. Push of a brand-new type after exit() throws (no silent auto-create) ---
    {
        Scope s;
        s.push<Small>(Small{1});
        s.exit();
        assert(throws([&] { s.push<Big>(Big{1, 2, 3, 4}); }));
        printf("[ok] push of a never-before-used type after exit() throws\n");
    }

    // --- 7. moveTo with an exited source throws (via poisoned pool) ---
    {
        Scope src, dst;
        Handle<Small> h = src.push<Small>(Small{1});
        src.exit();
        assert(throws([&] { h.moveTo(dst); }));
        printf("[ok] moveTo() with exited source throws\n");
    }

    // --- 8. moveTo into an exited destination throws (via dest's gate) ---
    {
        Scope src, dst;
        Handle<Small> h = src.push<Small>(Small{1});
        dst.exit();
        assert(throws([&] { h.moveTo(dst); }));
        printf("[ok] moveTo() into exited destination throws\n");
    }

    // --- 9. exit() is idempotent ---
    {
        Scope s;
        s.push<Small>(Small{1});
        s.exit();
        s.exit();
        s.exit();
        printf("[ok] exit() is safe to call more than once\n");
    }

    // --- 10. moveTo mutates the handle in place; one handle, no shell ---
    {
        destructedSmall = 0;
        Scope outer;
        Handle<TrackedSmall> moved;  // declared outside inner's block so it outlives it
        {
            Scope inner;
            moved = inner.push<TrackedSmall>(11);
            inner.push<TrackedSmall>(22);
            moved.moveTo(outer);           // moved now points into outer, same variable
            assert(destructedSmall == 1);  // old slot's object destructed immediately
            assert(moved->v == 11);        // still usable right here, already in outer
            inner.exit();                  // destroys the un-moved one (22); doesn't touch moved
            assert(destructedSmall == 2);
        }
        assert(moved.get()->v == 11);  // inner is gone, moved is still valid via outer
        outer.exit();
        assert(destructedSmall == 3);
        printf("[ok] moveTo mutates the handle in place, no second handle needed\n");
    }

    // --- 11. Mixed-size types resolve independently through their own handles ---
    {
        Scope s;
        Handle<Small> sh = s.push<Small>(Small{5});
        Handle<Big> bh = s.push<Big>(Big{1, 2, 3, 4});
        assert(sh->v == 5);
        assert(bh->c == 3);
        printf("[ok] independent per-type handles resolve correctly\n");
    }

    // --- 12. Replacing an exited Scope in a container starts genuinely fresh ---
    {
        destructedSmall = 0;
        std::vector<Scope> scopes;
        scopes.emplace_back(64);
        scopes[0].push<TrackedSmall>(1);
        scopes[0].push<TrackedSmall>(2);
        scopes[0].exit();
        assert(destructedSmall == 2);

        scopes[0] = Scope(64);
        Handle<TrackedSmall> h = scopes[0].push<TrackedSmall>(3);
        assert(h->v == 3);
        printf("[ok] replacing an exited Scope via container assignment works cleanly\n");
    }

    // --- 13. Same pattern via a hash map keyed by id ---
    {
        std::unordered_map<int, Scope> scopeMap;
        scopeMap.emplace(1, Scope(64));
        Handle<Small> h1 = scopeMap.at(1).push<Small>(Small{10});
        assert(h1->v == 10);
        scopeMap.at(1).exit();
        assert(throws([&] { h1.get(); }));

        scopeMap[1] = Scope(64);
        Handle<Small> h2 = scopeMap.at(1).push<Small>(Small{20});
        assert(h2->v == 20);
        printf("[ok] scope-per-key hash map with exit()+replace pattern works\n");
    }

    printf("\nAll tests passed.\n");
    return 0;
}