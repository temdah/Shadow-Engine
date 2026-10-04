/* Desktop Lua fixture only; does not certify the game's modified VM. */
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int main(int argc,char **argv)
{
    lua_State *state=luaL_newstate();
    int result;
    if(!state || argc!=2) return 2;
    luaL_openlibs(state);
    result=luaL_loadfile(state,argv[1],NULL);
    if(!result) result=lua_pcall(state,0,0,0);
    if(result) fprintf(stderr,"%s\n",lua_tostring(state,-1));
    lua_close(state);
    return result?1:0;
}
