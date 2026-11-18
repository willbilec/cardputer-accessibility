#pragma once
#include <cstddef>
namespace catalogueUi {
enum class Key { Up, Down, Enter, Other };
enum class Action { None, Focus, Select, Install, Help, Cancel };
struct Reply { Action action; size_t index; };
class Controller {
 size_t count;
 int pending=-1;
public:
 size_t selection=0;
 explicit Controller(size_t n):count(n) {}
 bool askInstall(size_t index) { if(index>=count) return false;pending=int(index);return true; }
 Reply handle(Key key) {
  if(pending>=0) { size_t index=pending;pending=-1;return {key==Key::Enter?Action::Install:Action::Cancel,index}; }
  if(key==Key::Up || key==Key::Down) {
   selection=(selection+(key==Key::Up?count:1))%(count+1);return {Action::Focus,selection};
  }
  if(key==Key::Enter) return {selection==count?Action::Help:Action::Select,selection};
  return {Action::None,selection};
 }
};
}
