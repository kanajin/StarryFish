#pragma once
#include <cmath>
#include <cstdint>
#include <array>
#include <charconv>
#include <string>
#include <stdexcept>

namespace gbrowser::webview::characters {
constexpr size_t kCount = 23;
struct Bindings : std::array<unsigned, kCount> {
  std::array<bool, kCount> enabled{};
  bool master = false;
  bool operator==(const Bindings& other) const {
    return static_cast<const std::array<unsigned,kCount>&>(*this) == static_cast<const std::array<unsigned,kCount>&>(other) && enabled == other.enabled && master == other.master;
  }
};
inline Bindings Defaults() {
  Bindings keys; static_cast<std::array<unsigned,kCount>&>(keys) = {'1','2','3','4',0x74,0xC0,'C','Z','Q','W','E','R','1','2','3','B','A','F','V',0x1B,'4','5','6'};
  keys.enabled.fill(true); return keys;
}
inline unsigned Scope(size_t i) { return i < 4 ? 3u : i >= 8 && i < 12 ? 2u : (i >= 12 && i < 15) || i >= 20 ? 4u : i == 4 ? 7u : 3u; }
inline unsigned NormalizeKey(unsigned key) {
  if (key == 0xA0 || key == 0xA1) return 0x10;
  if (key == 0xA2 || key == 0xA3) return 0x11;
  if (key == 0xA4 || key == 0xA5) return 0x12;
  return key;
}
inline bool Modifier(unsigned key) { return key == 0x10 || key == 0x11 || key == 0x12 || key == 0x5B || key == 0x5C; }
inline bool AllowedKey(unsigned key) { return key >= 8 && key <= 254 && key != 0xE5 && key != 0xE7 && NormalizeKey(key) == key; }
inline int Conflict(const Bindings& keys, size_t index, unsigned key) {
  for (size_t i = 0; i < keys.size(); ++i)
    if (i != index && key && keys[i] == key && keys.enabled[i] && keys.enabled[index] && (Scope(i) & Scope(index))) return static_cast<int>(i);
  return -1;
}
inline bool Valid(const Bindings& keys) {
  for (size_t i = 0; i < keys.size(); ++i)
    if ((keys[i] && !AllowedKey(keys[i])) || Conflict(keys,i,keys[i]) >= 0) return false;
  return true;
}
inline unsigned Actions(const Bindings& keys, unsigned key) {
  unsigned mask = 0;
  for (size_t i = 0; key && i < keys.size(); ++i) if (keys[i] == key && keys.enabled[i]) mask |= 1u << i;
  return mask;
}
inline std::string Encode(const Bindings& keys) {
  if (!Valid(keys)) throw std::runtime_error("同一界面的快捷键重复或无效");
  std::string text = keys.master ? "SFKEYS3,1" : "SFKEYS3,0";
  for (size_t i=0; i<keys.size(); ++i) text += "," + std::to_string(keys[i]) + ":" + (keys.enabled[i] ? "1" : "0");
  return text;
}
inline Bindings Decode(const std::string& text) {
  const bool v1=text.rfind("SFKEYS1,",0)==0, v2=text.rfind("SFKEYS2,",0)==0;
  const bool v3=text.rfind("SFKEYS3,",0)==0;
  if (text.size()>256 || (!v1&&!v2&&!v3)) throw std::runtime_error("快捷键配置格式错误");
  Bindings keys=Defaults();size_t begin=v3?10:8;const size_t count=v1?8:v2?20:kCount;
  if(v3) {
    if(text.size()<10 || (text[8]!='0'&&text[8]!='1') || text[9]!=',') throw std::runtime_error("快捷键总开关格式错误");
    keys.master=text[8]=='1';
  }
  for(size_t i=0;i<count;++i) {
    const size_t end=text.find(',',begin),stop=end==std::string::npos?text.size():end;
    if(begin>=stop || (i==count-1)!=(end==std::string::npos))throw std::runtime_error("快捷键配置格式错误");
    const size_t colon=v1?stop:text.find(':',begin);
    if(colon>stop || (!v1&&(colon+2!=stop || (text[colon+1]!='0'&&text[colon+1]!='1'))))throw std::runtime_error("快捷键配置格式错误");
    const auto parsed=std::from_chars(text.data()+begin,text.data()+colon,keys[i]);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+colon || colon==begin || (keys[i]&&!AllowedKey(keys[i])))throw std::runtime_error("快捷键配置格式错误");
    if(!v1)keys.enabled[i]=text[colon+1]=='1';begin=stop+1;
  }
  if(v1||v2) {
    const unsigned old[]{'1','2','3','4','R',0xC0,'Q','Z','1','2','3','4','1','2','3','E','A','F','W',0x1B};
    const auto previous_scope=[](size_t i){return i<4?1u:i>=8&&i<12?2u:i>=12&&i<15?4u:i==4?7u:3u;};
    for(size_t i=0;i<count;++i) for(size_t j=0;j<i;++j)
      if(keys[i] && keys[i]==keys[j] && keys.enabled[i] && keys.enabled[j] && (previous_scope(i)&previous_scope(j)))
        throw std::runtime_error("旧快捷键配置重复");
    const auto defaults=Defaults();
    std::array<bool,kCount> custom{};
    for(size_t i=0;i<count;++i) {custom[i]=keys[i]!=old[i];if(!custom[i])keys[i]=defaults[i];}
    // Custom bindings win over migrated defaults; character bindings win if
    // two old custom choices now overlap after restoring character switching.
    for(size_t i=0;i<keys.size();++i)for(size_t j=0;j<i;++j) {
      if(keys[i] && keys[i]==keys[j] && keys.enabled[i] && keys.enabled[j] && (Scope(i)&Scope(j))) {
        if(custom[i]&&!custom[j])keys.enabled[j]=false;else keys.enabled[i]=false;
      }
    }
  }
  if(!Valid(keys))throw std::runtime_error("快捷键重复或无效");
  return keys;
}
// Bounded FIFO of explicit key gestures, never autorepeat or saved coordinates.
struct InputQueue {
  static constexpr size_t capacity=32;
  std::array<unsigned,capacity> entries{};
  size_t head=0,size=0;
  bool Push(unsigned action) {
    if(!action || size==capacity)return false;
    entries[(head+size)%capacity]=action;++size;return true;
  }
  unsigned Pop() {
    if(!size)return 0;
    const auto action=entries[head];head=(head+1)%capacity;--size;return action;
  }
  void Clear() {head=0;size=0;}
};
struct KeyTracker {
  std::array<bool, 256> down{};
  unsigned modifier_candidate = 0;
  unsigned Feed(unsigned key, bool released, bool external_modifier = false) {
    key = NormalizeKey(key);
    if (key >= down.size()) return 0;
    const bool repeated = down[key]; down[key] = !released;
    bool other_modifier = false;
    for (unsigned m : {0x10u, 0x11u, 0x12u, 0x5Bu, 0x5Cu}) if (m != key && down[m]) other_modifier = true;
    if (released) {
      const bool single = Modifier(key) && modifier_candidate == key && repeated && !other_modifier;
      if (modifier_candidate == key) modifier_candidate = 0;
      return single ? key : 0;
    }
    if (repeated) return 0;
    if (Modifier(key)) {
      bool other_key = false;
      for (unsigned i = 0; i < down.size(); ++i) if (i != key && down[i]) other_key = true;
      modifier_candidate = other_key ? 0 : key; return 0;
    }
    modifier_candidate = 0;
    return other_modifier || external_modifier ? 0 : key;
  }
};
// One gesture permits Back, target, and (only for FC) its dedicated confirmation.
// Two matching geometry observations avoid clicking a panel still animating.
struct Motion {
  bool active = false;
  double from_x=0, from_y=0, to_x=0, to_y=0, bend=0;
  uint64_t started=0, duration=0;
  double speed=1;
  void Begin(double x, double y, double tx, double ty, uint64_t now, unsigned seed) {
    active=true;from_x=x;from_y=y;to_x=tx;to_y=ty;started=now;
    const double distance=std::hypot(tx-x,ty-y);
    // Initial input was instantaneous; use a near-instant 8 ms reference.
    speed=.9+(seed%101)/1000.0;
    duration=distance<2?0:static_cast<uint64_t>(std::ceil(8/speed));
    bend=(static_cast<int>((seed>>8)%201)-100)/100.0 * (distance*.08<20?distance*.08:20);
  }
  bool Done(uint64_t now) const {return now-started>=duration;}
  std::array<double,2> At(uint64_t now) const {
    if(Done(now))return {to_x,to_y};
    const double t=static_cast<double>(now-started)/duration, u=t*t*(3-2*t);
    const double dx=to_x-from_x,dy=to_y-from_y,d=std::hypot(dx,dy);
    const double offset=2*u*(1-u)*bend;
    return {from_x+dx*u-(d?dy/d*offset:0),from_y+dy*u+(d?dx/d*offset:0)};
  }
};
struct Sequence {
  int phase = 0, previous_action = 0;
  double previous_x = 0, previous_y = 0;
  uint64_t started = 0;
  bool allow_confirmation = false;
  bool Expired(uint64_t now) const { return now - started >= (allow_confirmation ? 3000u : 1500u); }
  bool Click(int action, double x, double y) {
    if (!((action == 2 && phase == 0) || (action == 3 && phase <= 1) ||
        (action == 6 && (phase == 0 || phase == 2) && allow_confirmation))) {
      previous_action = 0; return false;
    }
    if (previous_action != action || std::abs(x - previous_x) > 0.5 || std::abs(y - previous_y) > 0.5) {
      previous_action = action; previous_x = x; previous_y = y; return false;
    }
    previous_action = 0;
    phase = action == 2 ? 1 : action == 6 ? 3 : 2;
    return true;
  }
};
}
