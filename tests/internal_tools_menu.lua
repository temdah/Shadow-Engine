-- Fake host/filesystem integration. No game or native bridge.
local entry=assert(loadfile('src/internal/entrypoint.lua'))
local traffic=assert(loadfile('src/internal/traffic.lua'))
local settings=assert(loadfile('src/internal/settings.lua'))
local support=assert(loadfile('src/internal/support.lua'))
local realIO, realDofile=io,dofile
local files,notices,states,callbacks,roots,buttons,categories,scripts={},{},{},{},{},{},{},{}
local opens,writes,restores,suppressions,checks,clock=0,0,0,0,0,0
local scriptStates,timerQueue,setterQueue={},{},{}
local paused,deferSetter=true,false
local setterCalls=0
local fault,seq,rev,processed,result,pending=0,0,7,0,0,0
local pid,session,disabled,limit,vehicle,world,worldEnabled,ready,downstream=123,456,1,4,4096,4096,1,1,2
local serial,notice,loadReason=0,0,0
local selectedOverride,driverReady,extraSlots=nil,1,2
local populationProtocol,populationSerial,populationState,populationPartial,populationMarked=false,'0',0,0,0
local populationWindowMs,populationSampleMs,populationElapsedMs=60000,50,0
local hostCompatibility=0
local statusPath,requestPath='ShadowEngineDiagnostics.status','ShadowEngineDiagnostics.request'
local function check(x,msg) checks=checks+1; assert(x,msg or debug.traceback('check '..checks,2)) end
package.loadlib=function() error('native bridge forbidden') end
SH_LOG=function() end
SH_Notifications_PushNotification=function(x) notices[#notices+1]=x end
SH_Menu_RegisterMenu=function() return {} end
SH_Menu_RegisterCategory=function(name)
 categories[#categories+1]=name
 local root={lines={},widgets={}}
 root.Text=function(s,x) s.lines[#s.lines+1]=x end
 root.Divider=function() end
 root.Button=function(_,label,fn) buttons[label]=fn end
 root.CommandWidget=function(s,id) s.widgets[id]=true end
 root.Rerender=function(s) s.lines={}; s.layout(s) end
 roots[name]=root
 return {Layout=function(_,fn) root.layout=fn; fn(root) end}
end
SH_Commands_RegisterInt=function(id,label,description,menu,value,minimum,maximum,fn)
 check(not callbacks[id] and type(label)=='string' and type(description)=='string' and type(menu)=='table')
 check(value==0 and minimum==0 and maximum==10 and type(fn)=='function')
 states[id]=value; callbacks[id]=fn; fn() -- Adversarial eager registration callback.
end
SH_Commands_GetStateInt=function(id) return states[id] end
SH_Commands_SetStateInt=function(id,value)
 check(type(id)=='string' and type(value)=='number' and value==math.floor(value) and value>=0 and value<=10)
 states[id]=value; setterCalls=setterCalls+1
 if deferSetter then setterQueue[#setterQueue+1]=callbacks[id] else callbacks[id]() end
 -- Installed host returns no Lua values and fires even for unchanged values.
end
local function drainSetters() local old=setterQueue; setterQueue={}; for _,fn in ipairs(old) do fn() end end
SH_NextTick=function(fn,delay) check(type(fn)=='function' and delay==30); timerQueue[#timerQueue+1]=fn end
SH_Commands_RegisterBool=function(id,_,_,_,value,fn) check(not callbacks[id]); states[id]=value; callbacks[id]=fn; fn() end
SH_Commands_GetStateBool=function(id) return states[id] end
local function scriptDispatch(s,key) local fn=scriptStates[s][key]; if fn then fn(s) end end
Script=function(name)
 local s={name=name}; local registered={}; scriptStates[s]=registered
 setmetatable(s,{__newindex=function(_,key,value) registered[key]=value end,
  __index=function(_,key)
   if key=='Remove' then return function() scriptDispatch(s,'OnUnload'); registered.removed=true end end
   if key:sub(1,2)=='On' then return function() end end -- Real host callback getters are placeholders.
   return registered[key]
  end})
 scripts[#scripts+1]=s; return s
end
ChangeVehiclesBudget=function(n) check(n==0); suppressions=suppressions+1 end
RestoreBudgets=function() restores=restores+1 end
dofile=function(path)
 if path=='shadow_engine_tools/traffic.lua' then return traffic() end
 if path=='shadow_engine_tools/support.lua' then return support() end
 check(path=='shadow_engine_tools/settings.lua'); return settings()
end
io={open=function(path,mode)
 opens=opens+1
 if mode=='rb' then if not files[path] then return nil end; return {read=function(_,n) return files[path]:sub(1,n) end,close=function() return true end} end
 check(mode=='wb'); if fault==1 then return nil end
 return {write=function(_,text) writes=writes+1; if fault==2 then return nil end; files[path]=text; return true end,close=function() return fault~=3 end}
end}
os.rename=function(from,to) if fault==4 or files[to] then return nil end; files[to]=files[from]; files[from]=nil; return true end
os.time=function() return clock end
local function publish()
 local selected=selectedOverride or (disabled%2==1 and (limit==10 and 10 or 0) or limit)
 files[statusPath]=string.format('SE7 %s %s %d %d %d %d 2 0 0 %d %d %d %d %d %d %d %d %d %d %d 0 %d %d %d %d\n',pid,session,seq,disabled,ready,downstream,limit,vehicle,worldEnabled,world,serial,notice,processed,result,rev,rev,loadReason,pending,selected,driverReady,extraSlots)
 if populationProtocol then
  files[statusPath]=files[statusPath]:gsub('^SE7','SE'..populationProtocol):sub(1,-2)..string.format(' %s %d %d %d',populationSerial,populationState,populationPartial,populationMarked)
  if populationProtocol>=9 then files[statusPath]=files[statusPath]..string.format(' %d %d %d',populationWindowMs,populationSampleMs,populationElapsedMs) end
  if populationProtocol>=10 then files[statusPath]=files[statusPath]..string.format(' %d',hostCompatibility) end
  files[statusPath]=files[statusPath]..'\n'
 end
end
local tool
local function update()
 clock=clock+1
 if not paused then scriptDispatch(tool.script,'OnUpdate') end
 local old=timerQueue; timerQueue={}; for _,fn in ipairs(old) do fn() end
end
local function drag(value,deferred)
 states[tool.commands.limit]=value
 if deferred then setterQueue[#setterQueue+1]=callbacks[tool.commands.limit] else callbacks[tool.commands.limit]() end
end
local function pump() update(); update() end
local function finish(code) seq=seq+1; processed=seq; result=code; pending=0; if code==1 then rev=rev+1 end; files[requestPath]=nil; publish(); update() end
local function contains(page,text) return table.concat(roots[page].lines,'|'):find(text,1,true) end
local function expected(mask,a,b,c) return string.format('SES2 %d %d %d %d %d %d %d\n',pid,session,seq+1,mask,a or 0,b or 0,c or 0) end
entry("development"); tool=ShadowEngineDiagnostics
check(tool.loaded and #scripts==1 and opens==3 and writes==0 and restores==0 and suppressions==0)
check(table.concat(categories,'|')=='Shadows|Traffic Control|Diagnostics|Shadow onset recording')
check(not buttons['Apply world shadow settings'] and not buttons['Apply vehicle shadow settings'])
check(buttons['Check test progress']==tool.refreshCapture)
check(buttons['Toggle +2 extra slots']==tool.toggleExtras)
check(contains('Diagnostics','Tools v2.0.94 | Protocol SE10'))
check(buttons['Check shadow onset progress']==tool.refreshPopulation)
check(buttons['Start shadow onset recording']==tool.actions[14] and buttons['Stop shadow onset recording']==tool.actions[16])
check(not buttons['Mark slow shadow onset'] and not tool.actions[15] and buttons['Mark flicker now']==tool.actions[8])
check(not contains('Shadows','Reading saved engine settings') and not contains('Shadows','defaults') and not contains('Shadows','Active '))
check(buttons['Check current shadow settings']==tool.refresh and not buttons['Show applied engine settings'])
check(not contains('Shadows','Active ') and not roots['Shadows'].widgets[tool.commands.limit])
local initialOpens=opens; tool.script:OnUpdate(); check(opens==initialOpens) -- Getter is a dummy, not dispatch.
check(type(scriptStates[tool.script].OnUpdate)=='function' and paused and #timerQueue==1)
buttons['World: 512'](); check(writes==0)
-- Old status cannot distinguish zero from unlimited10 and must reject. The
-- equivalent SE7 status still hydrates with both host dispatchers withheld.
files[statusPath]='SE5 28704 6922359 0 0 1 2 2 0 0 4 2048 0 2048 0 0 0 0 0 0 1 0 0\n'
roots['Shadows']:Rerender(); check(tool.settings.unavailable and writes==0)
files[statusPath]='SE7 28704 6922359 0 0 1 2 2 0 0 4 2048 0 2048 0 0 0 0 0 0 1 0 0 4 1 2\n'
deferSetter=true; roots['Shadows']:Rerender()
check(tool.settings.current.pid=='28704' and tool.settings.current.session=='6922359')
check(states[tool.commands.limit]==4 and roots['Shadows'].widgets[tool.commands.limit])
check(tool.settings.value(1)==4 and writes==0 and #setterQueue>0)
drainSetters(); check(writes==0 and not next(tool.settings.desired))
entry("development"); check(ShadowEngineDiagnostics==tool and #scripts==1)
publish(); update(); drainSetters(); deferSetter=false; check(writes==0 and states[tool.commands.limit]==0)
local sharedOpens=opens; paused=false; update(); paused=true
check(opens==sharedOpens+1 and #timerQueue==1) -- NextTick owns polling; no second Script read.
local goodStatus=files[statusPath]
for _,mutation in ipairs({{1,'0'},{2,'-1'},{3,'4294967296'},{4,'4'},{5,'2'},
 {6,'-4'},{10,'0'},{10,'11'},{11,'8192'},{12,'2'},{13,'0'},{14,'-1'},
 {15,'6'},{16,'-1'},{17,'-5'},{17,'3'},{18,'-1'},{19,'-1'},{20,'5'},
 {21,'-1'},{22,'-1'},{23,'-1'},{23,'11'},{23,'10'},{23,'4'},
 {24,'-1'},{24,'2'},{25,'-1'},{25,'1'},{25,'3'},{25,'2147483647'},{1,string.rep('9',80)}}) do
 local fields={}; for field in goodStatus:sub(5):gmatch('[^ \n]+') do fields[#fields+1]=field end
 fields[mutation[1]]=mutation[2]
 check(not tool.settingsApi.parse('SE7 '..table.concat(fields,' ')..'\n',statusPath), 'invalid status field '..mutation[1]..'='..mutation[2])
end
check(not tool.settingsApi.parse(goodStatus..'\n',statusPath))
check(not tool.settingsApi.parse(goodStatus:sub(1,-2),statusPath))
check(not tool.settingsApi.parse(goodStatus:gsub('^SE7','SE5'),statusPath))
check(not tool.settingsApi.parse(goodStatus:gsub('^SE7','SE6'),statusPath))
check(not tool.settingsApi.parse(goodStatus:gsub(' [02]\n$', '\n'),statusPath))
check(not tool.settingsApi.parse(goodStatus:sub(1,-2)..' 2\n',statusPath))
check(tool.settingsApi.parse(goodStatus:gsub(' 2\n$', ' 0\n'),statusPath).extraSlots==0)
check(tool.settings.value(1)==0)
check(not contains('Shadows','Active quality:'))
local hostTostring=tostring
tostring=function(value) if type(value)=='number' then return string.format('%.6f',value) end return hostTostring(value) end
for _,root in pairs(roots) do root:Rerender() end
check(buttons['World: 512'] and not buttons['World: 512.000000'] and not contains('Shadows','4096.000000'))
tostring=hostTostring; check(writes==0)
buttons['Vehicle: 512'](); buttons['Vehicle: 1024'](); buttons['World: 2048'](); pump()
check(files[requestPath]==expected(6,0,2,3))
check(not contains('Shadows','Active quality:') and tool.settings.value(2)==2)
local sent=files[requestPath]
drag(1); drag(2); drag(3); update()
check(files[requestPath]==sent and tool.settings.value(1)==3)
tool.actions[7](); check(files[requestPath]==sent and notices[#notices]:find('pending'))
seq=1; pending=1; publish(); update(); check(tool.settings.inflight and not contains('Shadows','Settings saved and applied'))
vehicle=1024; world=2048; seq=0; finish(1); update(); check(files[requestPath]==expected(1,3))
limit=3; disabled=0; finish(1); check(tool.settings.value(1)==3 and states[tool.commands.limit]==3)
-- Save failure restores the actual slider. Deferred hydration callbacks read
-- current state; neither an old echo nor a later user drag can be lost/re-saved.
deferSetter=true; drag(7); pump(); check(files[requestPath]==expected(1,7)); finish(-3)
check(states[tool.commands.limit]==3 and tool.settings.value(1)==3 and #setterQueue>0)
drag(8); drag(9); drainSetters(); pump(); check(files[requestPath]==expected(1,9))
limit=9; finish(1); drainSetters(); check(states[tool.commands.limit]==9 and not next(tool.settings.desired))
-- Newer actual drags may wait in the host queue while an old native result
-- arrives first. Both failure and success must preserve the unobserved value.
for _,oldResult in ipairs({-3,1}) do
 drag(7); pump(); check(files[requestPath]==expected(1,7))
 drag(8,true); check(tool.settings.sliderObserved==7 and states[tool.commands.limit]==8)
 if oldResult==1 then limit=7 end
 finish(oldResult)
 check(states[tool.commands.limit]==8 and tool.settings.desired[1]==8)
 drainSetters(); update(); check(files[requestPath]==expected(1,8))
 limit=8; finish(1); drainSetters(); check(not tool.settings.inflight and not next(tool.settings.desired))
end
deferSetter=false
-- A failed hydration setter must not turn the old displayed request back into
-- a user change. A genuine later drag remains distinguishable from that state.
drag(7); pump()
local setInteger=SH_Commands_SetStateInt
SH_Commands_SetStateInt=function() error('setter unavailable') end
finish(-3)
check(tool.settings.sliderError and states[tool.commands.limit]==7 and not next(tool.settings.desired))
SH_Commands_SetStateInt=setInteger; roots['Shadows']:Rerender()
check(states[tool.commands.limit]==8 and not next(tool.settings.desired) and not files[requestPath])
drag(7); pump(); SH_Commands_SetStateInt=function() error('setter unavailable') end; finish(-3)
drag(9,true); SH_Commands_SetStateInt=setInteger; roots['Shadows']:Rerender()
check(tool.settings.desired[1]==9 and states[tool.commands.limit]==9)
drainSetters(); pump(); check(files[requestPath]==expected(1,9)); limit=9; finish(1)

buttons['Vehicle: 512'](); pump(); check(files[requestPath]==expected(2,0,1)); finish(-3)
check(not tool.settings.inflight and tool.settings.value(2)==2 and not contains('Shadows','Pending: 512'))
check(notices[#notices]:find('Previous saved and active values were retained',1,true))
buttons['Vehicle: 4096'](); pump(); vehicle=4096; finish(1); check(tool.settings.value(2)==4)
for _,key in ipairs({2,4}) do for value=0,4 do
 tool.settings.callbacks[key][value](); pump(); check(files[requestPath]==expected(key,0,key==2 and value or 0,key==4 and value or 0))
 if key==2 then disabled=value==0 and 2 or 0; if value>0 then vehicle=256*2^value end else worldEnabled=value==0 and 0 or 1; if value>0 then world=256*2^value end end
 finish(1)
end end
for value=0,10 do drag(value); pump(); check(files[requestPath]==expected(1,value)); disabled=(value==0 or value==10) and 1 or 0; if value>0 then limit=value end; finish(1) end
local unchangedWrites=writes; drag(10); pump(); check(writes==unchangedWrites and tool.settings.value(1)==10)
buttons['Check current shadow settings']()
check(notices[#notices]:find('Limiter: Unlimited (10)',1,true))
check(notices[#notices]:find('Driven-car protection: available',1,true))
check(contains('Shadows','1-9 counts traffic vehicles, not lights. 0 = limiter off; 10 = Unlimited.'))
-- Explicit selectedLimit keeps zero distinct even when its retained raw limit
-- is10. Read-only status changes must not be echoed into a settings request.
selectedOverride=0; publish(); update()
check(tool.settings.value(1)==0 and states[tool.commands.limit]==0 and writes==unchangedWrites)
buttons['Check current shadow settings']()
check(notices[#notices]:find('Limiter: 0 (limiter off)',1,true))
driverReady=0; publish(); buttons['Check current shadow settings']()
check(notices[#notices]:find('Driven-car protection: unavailable',1,true))
check(not contains('Shadows','Driven-car protection: unavailable'))
drag(10); pump(); check(files[requestPath]==expected(1,10))
selectedOverride=10; finish(1)
check(tool.settings.value(1)==10 and states[tool.commands.limit]==10)
-- An engine-loaded/migrated saved10 is authoritative; menu hydration never
-- submits defaults or rewrites it as zero. Malformed mixed semantics reject.
local beforeMigrationWrites=writes
loadReason=0; publish(); roots['Shadows']:Rerender()
check(tool.settings.current.savedRevision>0 and tool.settings.value(1)==10 and writes==beforeMigrationWrites)
local unlimitedStatus=files[statusPath]
check(not tool.settingsApi.parse(unlimitedStatus:gsub('^(SE7 %d+ %d+ %d+) 1 ', '%1 0 '),statusPath))
check(not tool.settingsApi.parse(unlimitedStatus:gsub(' 10 0 2\n$', ' 9 0 2\n'),statusPath))
check(not tool.settingsApi.parse(unlimitedStatus:gsub(' 10 0 2\n$', '\n'),statusPath))
selectedOverride=nil; driverReady=1
check(not tool.actions[5] and not buttons['Restore default vehicle settings']); drag(4); buttons['Vehicle: 2048'](); pump(); check(files[requestPath]==expected(3,4,3)); limit=4; disabled=0; vehicle=2048; finish(1)
check(tool.settings.value(1)==4 and states[tool.commands.limit]==4 and tool.settings.value(2)==3 and tool.settings.value(4)==4)
-- The temporary test control chooses from a fresh native read, waits for ACK,
-- reports actual values, and never writes a saved-settings record itself.
do
 local startingRevision=rev
 local function diagnostic(action)
  return string.format('SE1 %d %d %d %d\n',pid,session,seq+1,action)
 end
 local function ackExtras(value,advance)
  extraSlots=value; seq=seq+(advance or 1); files[requestPath]=nil; publish(); update()
 end
 check(tool.settings.current.extraSlots==2)
 extraSlots=0; publish() -- Cached presentation still says2; click must enable.
 buttons['Toggle +2 extra slots']()
 check(files[requestPath]==diagnostic(13) and tool.pending.extraSlots==2)
 check(notices[#notices]=='Extra-slot test request queued.')
 local waitingRequest=files[requestPath]; local waitingNotices=#notices
 update(); check(#notices==waitingNotices and tool.pending.extraSlots==2)
 buttons['Toggle +2 extra slots']()
 check(files[requestPath]==waitingRequest and notices[#notices]:find('pending',1,true))
 buttons['Check current shadow settings']()
 check(notices[#notices]:find('Extra slots: 0',1,true))
 ackExtras(2)
 check(notices[#notices]:find('Extra slots: 2',1,true) and tool.pending.extraSlots==nil)
 check(rev==startingRevision and not files[requestPath])
 local confirmedNotices=#notices; update(); check(#notices==confirmedNotices)

 buttons['Toggle +2 extra slots'](); check(files[requestPath]==diagnostic(12))
 files[statusPath]=nil; update()
 check(tool.pending.extraSlots==0 and tool.settings.unavailable)
 ackExtras(0)
 check(notices[#notices]:find('Extra slots: 0',1,true))
 buttons['Check current shadow settings']()
 check(notices[#notices]:find('Limiter: 4',1,true) and notices[#notices]:find('Extra slots: 0',1,true))
 check(not notices[#notices]:find('+2 transition',1,true))
 check(not contains('Diagnostics','Extra slots: 0') and not contains('Shadows','Extra slots: 0'))

 buttons['Toggle +2 extra slots'](); check(files[requestPath]==diagnostic(13))
 ackExtras(0) -- Native ACK alone must not manufacture the requested value.
 check(notices[#notices]:find('Extra slots: 0',1,true) and notices[#notices]:find('not applied',1,true))
 buttons['Toggle +2 extra slots'](); ackExtras(2,2)
 check(notices[#notices]:find('superseded',1,true) and notices[#notices]:find('Extra slots: 2',1,true))
 check(rev==startingRevision)

 -- A queued settings change must not replace the diagnostic before ACK; the
 -- observed diagnostic result must survive the later settings submission.
 buttons['Toggle +2 extra slots'](); waitingRequest=files[requestPath]
 buttons['World: 512'](); pump(); check(files[requestPath]==waitingRequest)
 ackExtras(0)
 check(notices[#notices]:find('Extra slots: 0',1,true))
 check(files[requestPath]==expected(4,0,0,1))
 world=512; finish(1)
 buttons['World: 4096'](); pump(); world=4096; finish(1)

 -- A newly published ACK can be consumed by the next click before the timer
 -- runs. Its fresh state determines the inverse command.
 buttons['Toggle +2 extra slots'](); check(files[requestPath]==diagnostic(13))
 extraSlots=2; seq=seq+1; files[requestPath]=nil; publish()
 buttons['Toggle +2 extra slots'](); check(files[requestPath]==diagnostic(12))
 check(notices[#notices-1]:find('Extra slots: 2',1,true))
 ackExtras(0)

 buttons['Toggle +2 extra slots']()
 session=session+1; seq=0; processed=0; result=0; extraSlots=2
 files[requestPath]=nil; publish(); update()
 check(notices[#notices]:find('new game session',1,true) and notices[#notices]:find('Extra slots: 2',1,true))
 check(tool.pending.extraSlots==nil and not files[requestPath])
 files[statusPath]=nil; buttons['Toggle +2 extra slots']()
 check(not files[requestPath] and notices[#notices]:find('unavailable',1,true))
 publish(); update()
 for failure=1,4 do
  fault=failure; buttons['Toggle +2 extra slots']()
  check(not files[requestPath] and tool.pending.extraSlots==nil and extraSlots==2)
 end
 fault=0
end
for failure=1,4 do fault=failure; buttons['World: 512'](); pump(); check(not tool.settings.inflight and not files[requestPath] and tool.settings.value(4)==4) end
fault=0; buttons['World: 512'](); pump(); sent=files[requestPath]; files[requestPath]=nil
for _=1,16 do update() end; check(files[requestPath]==sent and tool.settings.inflight); finish(-1); check(not tool.settings.inflight)
for _,failure in ipairs({-2,-4}) do
 buttons['World: 512'](); pump(); finish(failure)
 check(not tool.settings.inflight and tool.settings.value(4)==4)
end
-- A failed older save does not discard newer explicitly chosen values.
buttons['World: 512'](); pump(); buttons['World: 1024'](); update(); finish(-3); update()
check(files[requestPath]==expected(4,0,0,2))
world=1024; finish(1); check(tool.settings.value(4)==2)
-- Worker status can skip directly to saved; no intermediate states are required.
buttons['World: 4096'](); pump(); world=4096; finish(1); check(tool.settings.value(4)==4)
for _,action in ipairs({1,2,3,4,6,7,8,9,10,11}) do
 local before=#notices; tool.actions[action](); check(files[requestPath]==string.format('SE1 %d %d %d %d\n',pid,session,seq+1,action)); check(#notices==before+1)
 seq=seq+1; files[requestPath]=nil; publish()
end
local before=#notices; serial=1; notice=1; publish(); update(); check(#notices==before)
notice=2; publish(); update(); check(notices[#notices]:find('is recording',1,true)); before=#notices; update(); check(#notices==before)
buttons['Check test progress'](); check(#notices==before+1 and not files[requestPath] and tool.captureWatch.serial==1)
tool.actions[11](); check(not files[requestPath] and notices[#notices]:find('already running'))
notice=3; publish(); update(); check(notices[#notices]:find('Saving'))
notice=4; publish(); update(); check(not tool.captureWatch and tool.captureMessage:find('Ready'))
tool.actions[11](); seq=seq+1; files[requestPath]=nil; serial=2; notice=5; publish(); update(); check(tool.captureMessage:find('capture failed',1,true))
tool.actions[11](); seq=seq+1; files[requestPath]=nil; files[statusPath]=nil; update(); check(tool.captureWatch and tool.captureMessage:find('unavailable'))
serial=3; notice=2; publish(); update(); check(tool.captureMessage:find('is recording',1,true))
check(not contains('Shadows','Engine status unavailable. Reopen'))
local notification=SH_Notifications_PushNotification; SH_Notifications_PushNotification=function() error('popup unavailable') end
notice=4; publish(); update(); check(not tool.captureWatch and tool.captureMessage:find('Ready')); SH_Notifications_PushNotification=notification
buttons['World: 512'](); pump(); buttons['World: 1024'](); drag(8,true)
pid=222; session=777; seq=0; processed=0; result=0; serial=0; notice=0; disabled=1; vehicle=4096; world=4096; files[requestPath]=nil; publish(); pump()
drainSetters()
check(not tool.settings.inflight and not next(tool.settings.desired) and not files[requestPath] and states[tool.commands.limit]==0)
check(tool.settings.value(1)==0)
downstream=-3; publish(); update(); buttons['World: 512'](); pump(); check(files[requestPath]==expected(4,0,0,1)); world=512; finish(1)
buttons['Check current shadow settings'](); check(notices[#notices]:find('Shadow processing: not ready',1,true))
tool.actions[7](); check(not files[requestPath] and notices[#notices]:find('unavailable'))
buttons['Toggle +2 extra slots'](); check(not files[requestPath] and notices[#notices]:find('unavailable'))
downstream=2; ready=0; publish(); update(); buttons['Check current shadow settings'](); check(notices[#notices]:find('Shadow processing: not ready',1,true))
files[statusPath]='SE4 222 777 0 1 1 2 2 0 0 4 2048 0 2048 0 0\n'; update(); buttons['World: 512'](); check(not files[requestPath] and tool.settings.unavailable)
ready=1; publish(); update(); os.time=function() error('clock unavailable') end
local oldOpens=opens; update(); update(); check(not tool.settings.unavailable and opens>oldOpens)
-- Reload can lose Lua pending while status still has the previous ACK. Native
-- replay rejection must not make the old request look like the new choice saved.
os.time=function() return clock end; tool.statusWatch=nil
buttons['World: 512'](); pump(); sent=files[requestPath]
files[requestPath]=nil -- The old native worker accepted it; status is still stale.
tool.menu:OnRemove(); callbacks={}; entry("development"); tool=ShadowEngineDiagnostics; update()
buttons['World: 1024'](); pump()
check(files[requestPath]==expected(4,0,0,2))
buttons['World: 2048'](); world=512; finish(1)
check(notices[#notices]:find('Another request completed first',1,true))
update(); check(files[requestPath]==expected(4,0,0,3))
world=2048; finish(1); check(tool.settings.value(4)==3)
-- Preserve DWORD identifiers as strings on a single-precision Lua host.
drag(7,true)
session='4000000001'; seq=0; processed=0; result=0; publish(); drainSetters(); update()
check(not next(tool.settings.desired) and not files[requestPath] and states[tool.commands.limit]==0)
buttons['World: 512'](); pump()
check(files[requestPath]=='SES2 222 4000000001 1 4 0 0 1\n')
world=512; finish(1)
-- With both host dispatchers withheld, page/actions still hydrate read-only and
-- show explicit pending state. The final edit is sent when that queue resumes.
limit=6; disabled=0; publish(); roots['Shadows']:Rerender()
check(states[tool.commands.limit]==6 and not contains('Shadows','Active vehicle limit:'))
buttons['World: 1024'](); buttons['World: 2048']()
check(not files[requestPath] and tool.settings.desired[4]==3 and not contains('Shadows','Change pending'))
pump(); check(files[requestPath]:sub(-9)==' 4 0 0 3\n')
world=2048; finish(1); check(tool.settings.value(4)==3 and not tool.settings.inflight)
-- P-A controls own their protocol, request result, serial and progress. The
-- unchanged SE7 still supports every legacy setting/control above.
for _,action in ipairs({14,16}) do
 local oldWrites=writes; tool.actions[action]()
 check(writes==oldWrites and notices[#notices]:find('requires Shadow Engine 2.0.87',1,true))
end
buttons['Check shadow onset progress'](); check(not tool.populationWatch and notices[#notices]:find('requires Shadow Engine 2.0.87',1,true))
-- SE8 remains usable for settings but cannot promise whole-pass recording.
populationProtocol=8; publish(); update()
check(tool.settingsApi.parse(files[statusPath],statusPath) and not tool.settings.current.populationAvailable)
for _,action in ipairs({14,16}) do
 local oldWrites=writes; tool.actions[action]()
 check(writes==oldWrites and notices[#notices]:find('requires Shadow Engine 2.0.87',1,true))
end
populationProtocol=9; publish(); update()
local onsetStatus=files[statusPath]
local decoded=tool.settingsApi.parse(onsetStatus,statusPath)
check(decoded.populationAvailable and decoded.populationSerial=='0' and decoded.populationState==0 and decoded.populationWindowMs==60000 and decoded.populationSampleMs==50 and decoded.populationElapsedMs==0)
for _,mutation in ipairs({{26,'-1'},{26,'4294967296'},{27,'-1'},{27,'7'},{28,'2'},{28,'-1'},{29,'2'},{29,'-1'},
 {30,'0'},{30,'-1'},{30,'59999'},{30,'60001'},{31,'0'},{31,'-1'},{31,'100'},{32,'-1'},{32,'60001'},{32,'4294967296'}}) do
 local fields={}; for field in onsetStatus:sub(5):gmatch('[^ \n]+') do fields[#fields+1]=field end
 fields[mutation[1]]=mutation[2]
 check(not tool.settingsApi.parse('SE9 '..table.concat(fields,' ')..'\n',statusPath))
end
check(not tool.settingsApi.parse(onsetStatus:gsub('^SE9','SE7'),statusPath))
check(not tool.settingsApi.parse(onsetStatus:gsub('^SE9','SE8'),statusPath))
check(not tool.settingsApi.parse(onsetStatus:gsub(' 60000 50 0\n$','\n'),statusPath))
populationState=1; publish(); check(not tool.settingsApi.parse(files[statusPath],statusPath)); populationState=0; publish()
local function onsetResult(code,newSerial,newState,newPartial,newMarked)
 seq=seq+1; processed=seq; result=code; files[requestPath]=nil
 populationSerial=newSerial or populationSerial; populationState=newState or populationState
 populationPartial=newPartial or 0; populationMarked=newMarked or 0
 publish(); update()
end
local function onsetRequest(action)
 tool.actions[action](); check(files[requestPath]==string.format('SE1 %s %s %d %d\n',pid,session,seq+1,action))
end
local onsetWrites=writes
tool.actions[16](); check(writes==onsetWrites)
onsetRequest(14); check(tool.populationWatch.serial=='1' and tool.populationWatch.request and not tool.captureWatch)
onsetWrites=writes; tool.actions[14](); check(writes==onsetWrites and notices[#notices]:find('pending',1,true))
seq=seq+1; files[requestPath]=nil; populationSerial='1'; populationState=1; publish(); update()
check(tool.populationWatch.request and tool.populationMessage:find('Waiting for engine confirmation',1,true))
processed=seq; result=1; publish(); update()
check(not tool.populationWatch.request and tool.populationMessage:find('60 seconds remaining',1,true))
local onsetNotices=#notices; update(); check(#notices==onsetNotices)
populationElapsedMs=1200; publish(); update()
check(tool.populationMessage:find('59 seconds remaining',1,true) and #notices==onsetNotices)
populationElapsedMs=41000; publish(); update()
check(contains('Shadow onset recording','19 seconds remaining') and #notices==onsetNotices)
serial=serial+1; notice=4; publish(); update()
check(#notices==onsetNotices and tool.populationWatch and not tool.populationMessage:find('saved',1,true))
-- Legacy flicker commands neither start nor complete the onset watch.
tool.actions[7](); check(files[requestPath]==string.format('SE1 %s %s %d 7\n',pid,session,seq+1))
seq=seq+1; processed=seq; result=1; files[requestPath]=nil; publish(); update()
check(tool.populationWatch.serial=='1' and tool.populationState==nil)
-- Compatibility annotations never change the whole-pass deadline or reintroduce
-- a reactive mark instruction into this menu.
for _,legacyState in ipairs({2,6}) do
 populationState=legacyState; populationMarked=1; publish(); update()
 check(tool.populationWatch and tool.populationMessage:find('19 seconds remaining',1,true) and not tool.populationMessage:find('mark',1,true))
end
populationState=1; populationMarked=0; publish(); update()
-- No second button is needed: native elapsed time and automatic state changes
-- complete an unmarked full pass, with no invented local completion.
populationElapsedMs=60000; publish(); update()
check(tool.populationWatch and tool.populationMessage:find('Waiting for automatic saving',1,true))
onsetWrites=writes; populationState=3; publish(); update()
check(writes==onsetWrites and populationMarked==0)
check(tool.populationMessage:find('Saving the report',1,true))
onsetWrites=writes; tool.actions[14](); check(writes==onsetWrites and notices[#notices]:find('Cannot start another',1,true))
populationState=4; publish(); update()
check(not tool.populationWatch and tool.populationMessage:find('report saved',1,true) and not tool.populationMessage:find('unmarked',1,true))
onsetNotices=#notices; update(); check(#notices==onsetNotices)
buttons['Check shadow onset progress'](); check(#notices==onsetNotices+1 and not files[requestPath])
-- A native rejection wins over a stale successful report/optimistic request.
onsetRequest(14); onsetResult(-1,'1',4,0,0)
check(not tool.populationWatch and tool.populationMessage:find('was rejected',1,true))
onsetRequest(14); populationElapsedMs=0; onsetResult(1,'2',1,0,0)
populationElapsedMs=14500; publish(); update(); onsetRequest(16); onsetResult(1,'2',3,0,0)
check(tool.populationMessage:find('Saving the report',1,true))
populationState=4; populationPartial=1; publish(); update()
check(not tool.populationWatch and tool.populationMessage:find('partial coverage or output',1,true))
onsetRequest(14); onsetResult(1,'3',5,0,0)
check(not tool.populationWatch and tool.populationMessage:find('saving failed',1,true))
-- Missing status never fabricates completion; restoration can skip states.
onsetRequest(14); files[statusPath]=nil; update()
check(tool.populationWatch and tool.populationMessage:find('status unavailable',1,true))
populationElapsedMs=0; onsetResult(1,'4',1,0,0)
check(tool.populationWatch and tool.populationMessage:find('recording is active',1,true))
populationState=4; publish(); update(); check(not tool.populationWatch)
onsetRequest(14); seq=seq+2; processed=seq; result=1; files[requestPath]=nil
populationSerial='5'; populationState=4; publish(); update()
check(not tool.populationWatch and tool.populationMessage:find('superseded',1,true))
-- The same request/result numbers in a new session cannot satisfy an old watch.
onsetRequest(14); session='4000000002'; seq=0; processed=0; result=0; files[requestPath]=nil
populationSerial='0'; populationState=0; publish(); update()
check(not tool.populationWatch and tool.populationMessage:find('new game session',1,true))
-- Exact serials above the single-precision boundary are not rounded or reused.
populationSerial='16777216'; populationState=4; publish(); update(); onsetRequest(14)
check(tool.populationWatch.serial=='16777217')
onsetResult(1,'16777217',1,0,0)
populationSerial='16777218'; populationState=4; publish(); update()
check(not tool.populationWatch and tool.populationMessage:find('identity changed',1,true))
populationSerial='4294967295'; publish(); onsetWrites=writes; tool.actions[14]()
check(writes==onsetWrites and notices[#notices]:find('sequence exhausted',1,true))
populationSerial='10'; populationState=1; publish(); buttons['Check shadow onset progress']()
check(tool.populationWatch.serial=='10')
populationState=4; populationMarked=1; publish(); update(); check(not tool.populationWatch)
check(contains('Shadow onset recording','whole 60-second pass') and contains('Shadow onset recording','No reaction or mark is required') and contains('Shadow onset recording','2.0.87'))
-- SE10 keeps legacy controls and adds a once-per-session compatibility warning.
populationProtocol=10; hostCompatibility=1; publish(); update()
check(tool.settingsApi.parse(files[statusPath]).hostCompatibility==1 and not tool.compatibilityMessage)
local compatibilityNotices,compatibilityWrites=#notices,writes
hostCompatibility=3; publish(); update()
check(#notices==compatibilityNotices+1 and notices[#notices]:find('1.1.13',1,true))
check(tool.compatibilityMessage:find('optional Lua repair is disabled',1,true))
update(); update(); check(#notices==compatibilityNotices+1 and writes==compatibilityWrites)
roots['Shadows']:Rerender(); check(contains('Shadows','revert to that complete version'))
for _,value in ipairs({-1,2,6}) do
 hostCompatibility=value; publish(); check(not tool.settingsApi.parse(files[statusPath]))
end
hostCompatibility=1; publish(); update(); check(not tool.compatibilityMessage)
hostCompatibility=4; session='4000000003'; publish(); update()
check(tool.compatibilityMessage and notices[#notices]:find('1.1.13',1,true))
hostCompatibility=1; publish(); update()
local retiredWrites=writes
check(not tool.settings.queueChoice(32,1))
check(not buttons['Background: Off'] and not buttons['Background: 512'] and not buttons['Background: 1024'])
check(not tool.settings.desired[32] and writes==retiredWrites)
states[tool.commands.traffic]=false; callbacks[tool.commands.traffic](); check(suppressions==1 and tool.traffic.suppressed)
states[tool.commands.traffic]=true; callbacks[tool.commands.traffic](); check(restores==1 and not tool.traffic.suppressed)
ChangeVehiclesBudget=function() error('traffic unavailable') end; states[tool.commands.traffic]=false; callbacks[tool.commands.traffic](); check(not tool.traffic.suppressed and notices[#notices]:find('failed'))
ChangeVehiclesBudget=function() suppressions=suppressions+1 end; callbacks[tool.commands.traffic]()
local fakeIO=io; io=nil; tool.refresh(); check(notices[#notices]:find('mailbox unavailable'))
tool.menu:OnRemove(); check(not tool.loaded and restores==2)
local removedWrites=writes; tool.toggleExtras(); check(writes==removedWrites)
tool.settings.choose(1,9); callbacks[tool.commands.traffic](); check(restores==2 and suppressions==2 and not next(tool.settings.desired))
local oldTimerCount=#timerQueue; update(); check(#timerQueue==0 and oldTimerCount==1)
-- When NextTick cannot be registered, only the retained Script callback is the
-- fallback. Its getter still must not be called by the implementation/mock.
io=fakeIO
local nextTick=SH_NextTick; SH_NextTick=nil
callbacks={}; entry("development"); tool=ShadowEngineDiagnostics
check(tool.loaded and #scripts==3 and not tool.timerAvailable and #timerQueue==0)
local fallbackOpens=opens
local observe=tool.settings.observe; tool.settings.observe=function() error('observer failure') end
clock=clock+1; scriptDispatch(tool.script,'OnUpdate'); check(not tool.reading)
tool.settings.observe=observe
for _=1,60 do scriptDispatch(tool.script,'OnUpdate') end
check(opens>fallbackOpens and #timerQueue==0)
tool.menu:OnRemove(); SH_NextTick=nextTick
-- Public entry keeps settings and traffic, with exactly one diagnostic action.
callbacks={};categories={};buttons={};roots={};states={};timerQueue={};setterQueue={}
pid,session,seq,processed,result,pending=123,456,0,0,0,0
disabled,limit,selectedOverride,vehicle,world,worldEnabled,ready,downstream=1,4,0,2048,2048,0,1,2
populationProtocol=10;hostCompatibility=1;populationSerial='0';populationState=0
files[requestPath]=nil; publish(); entry(); tool=ShadowEngineDiagnostics; pump()
check(tool.loaded and table.concat(categories,'|')=='Shadows|Traffic Control|Support')
check(buttons['Create support report'] and not buttons['Mark flicker now'] and not buttons['Check test progress'])
check(not buttons['Start shadow onset recording'] and not buttons['Save vehicle shadow sample'])
check(not buttons['Toggle +2 extra slots'] and buttons['World: 512'] and callbacks[tool.commands.traffic])
local supportStatus='ShadowEngineSupport.status'
local beforeSupportWrites=writes; buttons['Create support report']()
check(writes==beforeSupportWrites+1 and files[requestPath]=='SE1 123 456 1 17\n')
buttons['Create support report']();check(writes==beforeSupportWrites+1)
seq,processed,result=1,1,1;files[requestPath]=nil;publish()
files[supportStatus]='SEB1 123 999 1 2 10 0 0 none\n';pump()
check(tool.support.current==nil) -- Never borrow another session's report.
files[supportStatus]='SEB1 123 456 1 2 10 0 0 none\n';pump()
check(tool.support.current.phase==2 and contains('Support','Stage 2/4'))
files[supportStatus]='SEB1 123 456 1 6 78 16 0 ShadowEngine-Support-123-456-1.zip\n';pump()
check(not tool.support.request and contains('Support','missing or incomplete sections'))
check(contains('Support','ShadowEngine-Support-123-456-1.zip'))
local supportApi=support()
check(not supportApi.parse('SEB1 123 456 1 5 78 0 0 ../bad.zip\n',tool.settings.current))
check(not supportApi.parse('SEB1 123 456 1 5 78 0 0 none\n',tool.settings.current))
check(not supportApi.parse('SEB1 123 456 1 2 78 0 0 ShadowEngine-Support-123-456-1.zip\n',tool.settings.current))
buttons['Create support report']();seq,processed,result=2,2,-1;files[requestPath]=nil;publish();pump()
check(not tool.support.request and contains('Support','could not start'))
check(not supportApi.parse('SEB1 123 456 1 7 78 9999999999 0 none\n',tool.settings.current))
files[supportStatus]='SEB1 123 456 2 7 78 0 5 none\n';pump()
check(contains('Support','Could not save') and contains('Support','Error 5'))
buttons['Create support report']();check(tool.support.request)
session=789;publish();pump()
check(not tool.support.request and not tool.support.current and contains('Support','session changed'))
files[supportStatus]='SEB1 123 789 1 2 10 0 0 none\n';pump()
check(tool.support.current.phase==2) -- Reopening the menu can follow the worker.
beforeSupportWrites=writes;buttons['Create support report']();check(writes==beforeSupportWrites)
tool.menu:OnRemove()
io=realIO; dofile=realDofile
print('PASS menu: '..checks..' checks; public single staged support action, settings/traffic retained; session/result/error/repeat guards; development diagnostic regression coverage; desktop Lua only')
