/* Desktop compiler metadata gate for WD1's packed prototype fields.
 * This measures source requirements; it does not certify the target compiler. */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lstate.h"
#include "lobject.h"
static unsigned count, maximum_registers, maximum_upvalues, maximum_parameters, failed;
static void inspect(const Proto *p, const char *path) {
    int i;
    ++count;
    if (p->maxstacksize > maximum_registers) maximum_registers = p->maxstacksize;
    if (p->nups > maximum_upvalues) maximum_upvalues = p->nups;
    if (p->numparams > maximum_parameters) maximum_parameters = p->numparams;
    if (p->maxstacksize > 31 || p->nups > 15 || p->numparams > 15) failed = 1;
    printf("%s lines=%d-%d registers=%u upvalues=%u parameters=%u\n", path,
        p->linedefined, p->lastlinedefined, (unsigned)p->maxstacksize,
        (unsigned)p->nups, (unsigned)p->numparams);
    for (i = 0; i < p->sizep; ++i) inspect(p->p[i], path);
}
int main(int argc, char **argv) {
    int i;
    lua_State *state = luaL_newstate();
    if (!state || argc < 2) return 2;
    for (i = 1; i < argc; ++i) {
        if (luaL_loadfile(state, argv[i], NULL)) {
            fprintf(stderr, "%s\n", lua_tostring(state, -1));
            lua_close(state); return 2;
        }
        inspect(clvalue(state->top - 1)->l.p, argv[i]);
        lua_settop(state, 0);
    }
    lua_close(state);
    printf("%s prototypes=%u maximumRegisters=%u maximumUpvalues=%u maximumParameters=%u limits=31/15/15 scope=offlineLua51DerivedCompiler\n",
        failed ? "FAIL" : "PASS", count, maximum_registers, maximum_upvalues, maximum_parameters);
    return failed ? 1 : 0;
}
