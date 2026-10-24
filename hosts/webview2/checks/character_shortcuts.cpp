#include "character_shortcuts.h"
#include <stdexcept>

void RunCharacterShortcutChecks() {
  using namespace gbrowser::webview::characters;
  const auto require = [](bool ok) { if (!ok) throw std::runtime_error("character shortcut sequence check failed"); };
  require(Decode(Encode(Defaults())) == Defaults());
  auto custom = Defaults(); custom.master=true; custom[0] = 0x70; custom[1] = 0;
  require(Decode(Encode(custom)) == custom && Actions(custom, 0x70) == 1 && Actions(custom, '1') == (1u<<12));
  for (const auto& bad : {"", "SFKEYS1,1", "SFKEYS1,49,49,51,52,82,192,81,90", "SFKEYS1,49,50,51,52,82,192,81,999", "SFKEYS1,49,50,51,52,82,192,81,90,0"}) {
    bool rejected = false; try { Decode(bad); } catch (...) { rejected = true; } require(rejected);
  }
  require(Actions(Defaults(),'1')==((1u<<0)|(1u<<12)));
  auto disabled=Defaults();disabled.enabled[8]=false;
  require(Decode(Encode(disabled))==disabled && Actions(disabled,'1')==((1u<<0)|(1u<<12)));
  auto conflict=Defaults();conflict[16]='1';require(!Valid(conflict));conflict.enabled[16]=false;require(Valid(conflict));
  require(Decode("SFKEYS1,49,50,51,52,82,192,81,90")==Defaults());
  auto migrated=Decode("SFKEYS1,69,50,51,52,82,192,81,90"); require(migrated[0]=='E' && !migrated.enabled[10]);
  for (const auto& bad : {"SFKEYS2,49:2", "SFKEYS2,49:", "SFKEYS2,49", "SFKEYS2,:1", "SFKEYS2,0:10"}) {
    bool rejected=false;try{Decode(bad);}catch(...){rejected=true;}require(rejected);
  }
  auto overlap=Defaults();overlap[0]='Q';require(!Valid(overlap));overlap.enabled[8]=false;require(Valid(overlap));
  require(Defaults()[4]==0x74 && Defaults()[6]=='C' && Defaults()[15]=='B' && Defaults()[18]=='V');
  std::string old="SFKEYS2";
  for(unsigned key:{49,50,51,52,82,192,81,90,49,50,51,52,49,50,51,69,65,70,87,27})old+=","+std::to_string(key)+":1";
  require(Decode(old)==Defaults());
  for(const auto& bad:{"SFKEYS3,2,49:1","SFKEYS3,1", "SFKEYS3,1,49:1"}) {bool rejected=false;try{Decode(bad);}catch(...){rejected=true;}require(rejected);}
  InputQueue queue;
  require(!queue.Pop()&&!queue.Push(0));
  for(unsigned i=1;i<=InputQueue::capacity;++i)require(queue.Push(i));
  require(!queue.Push(99));
  for(unsigned i=1;i<=InputQueue::capacity;++i)require(queue.Pop()==i);
  require(!queue.Pop());
  // Wraparound and repeated explicit presses retain FIFO order.
  require(queue.Push(3)&&queue.Push(3)&&queue.Pop()==3&&queue.Push(8)&&queue.Pop()==3&&queue.Pop()==8);
  queue.Push(7);queue.Clear();require(!queue.Pop()&&queue.Push(9)&&queue.Pop()==9);
  KeyTracker tracker;
  require(tracker.Feed('R', false) == 'R' && !tracker.Feed('R', false) && !tracker.Feed('R', true));
  require(!tracker.Feed(0xA2, false) && tracker.Feed(0xA2, true) == 0x11);
  require(!tracker.Feed(0x11, false) && !tracker.Feed('R', false) && !tracker.Feed('R', true) && !tracker.Feed(0x11, true));
  require(!tracker.Feed('Q', false, true)); tracker.Feed('Q', true);
  tracker.Feed('A', false); require(!tracker.Feed(0x10, false) && !tracker.Feed(0x10, true)); tracker.Feed('A', true);
  for(unsigned key:{static_cast<unsigned>('A'),0x1Bu}) {
    KeyTracker input;
    require(input.Feed(key,false)==key && !input.Feed(key,false));
    require(!input.Feed(key,true) && input.Feed(key,false)==key);
    input={};require(input.Feed(key,false)==key); // focus loss clears stale edge state
  }
  Sequence s; s.started = 100;
  require(!s.Expired(1599) && s.Expired(1600));
  require(!s.Click(2, 10, 20) && !s.Click(2, 12, 20)); // moving panel
  require(s.Click(2, 12, 20) && s.phase == 1);
  require(!s.Click(2, 12, 20)); // never replay Back
  require(!s.Click(3, 30, 40) && s.Click(3, 30, 40) && s.phase == 2);
  require(!s.Click(3, 30, 40) && !s.Click(2, 12, 20)); // no retries after input
  Sequence direct;
  require(!direct.Click(3, 10, 20) && direct.Click(3, 10, 20) && direct.phase == 2);
  require(!direct.Click(6,10,20));
  Sequence fc;fc.allow_confirmation=true;
  require(!fc.Expired(2999)&&fc.Expired(3000));
  require(!fc.Click(3,10,20)&&fc.Click(3,10,20));
  require(!fc.Click(6,30,40)&&fc.Click(6,30,40)&&fc.phase==3&&!fc.Click(6,30,40));
  for(unsigned seed=0;seed<1000;++seed) {
    Motion motion;motion.Begin(10,20,500,400,100,seed);
    require(motion.speed>=.9&&motion.speed<=1 && motion.duration>=8&&motion.duration<=9);
    require(motion.At(100)==std::array<double,2>{10,20});
    require(motion.At(100+motion.duration)==std::array<double,2>{500,400});
    for(uint64_t t=0;t<=motion.duration;++t) {
      const auto p=motion.At(100+t);require(std::isfinite(p[0])&&std::isfinite(p[1])&&p[0]>=10&&p[0]<=500&&p[1]>=20&&p[1]<=400);
    }
  }
  Motion zero;zero.Begin(1,1,1,1,0,9);require(zero.Done(0)&&zero.At(0)==std::array<double,2>{1,1});
}
