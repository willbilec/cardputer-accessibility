// Semantic adapter over the original utility's state and handlers. Included
// after those state declarations, not from the graphics renderer.
#include "math_speech.h"
#include <limits>
static AccessibleKeys accessibleKeys;
static bool accessibleMenu=false;
static int accessibleMenuIndex=0;
static size_t accessibleCurve=0,accessiblePoint=0,accessibleConsole=0;
static const AppState accessibleModes[]={STATE_CALC,STATE_VARS,STATE_CONSTS,STATE_FORMULAS,STATE_SCRIPTS,STATE_PARAMS,STATE_PLOT,STATE_BINDS};
static const char*accessibleNames[]={"Calculator","Variables","Constants","Formula wizards","Scripts","Settings","Plot data","Keyboard bindings"};
static String accessibleNumber(double n){char buf[48];snprintf(buf,sizeof(buf),"%.*g",std::numeric_limits<double>::max_digits10,n);return String(buf);}
static String accessibleMath(const std::string&s){return String(MathSpeech::describe(s).c_str());}
static String accessibleMenuText(){return String("Tools. ")+accessibleNames[accessibleMenuIndex]+". "+String(accessibleMenuIndex+1)+" of 8. Semicolon up, period down. Enter opens. Control backtick cancels.";}
static String accessibleContext(){
 if(accessibleMenu)return accessibleMenuText();
 if(delete_confirm_prompt_mode)return String("Delete confirmation. ")+(delete_confirm_selected_idx==0?"Yes, delete selected. ":"No, cancel selected. ")+"Left and right choose. Enter confirms. Escape cancels.";
 if(show_help_popup){
  std::string title;std::vector<std::string> lines;getHelpPopupData(appState,script_edit_mode,title,lines);
  String text="Help. Control O opens the tools menu. Control Space repeats context. Control Shift Space reads more. Control Backspace stops speech. Control and semicolon, period, comma or slash provide arrow navigation. Control backtick goes back. ";
  text+=title.c_str();for(const auto&line:lines)text+=". "+String(line.c_str());
  text+=". Physical Function-key shortcuts are listed in the original help. Use the Control O tools menu on a desktop keyboard. Enter or Escape closes help.";return text;
 }
 switch(appState){
 case STATE_CALC:{
  String text="Calculator. ";text+=expression.empty()?String("Empty expression."):String("Expression. ")+accessibleMath(expression);
  text+=". Cursor "+String(cursor_pos+1)+" of "+String(expression.size()+1)+". ";
  if(!resultStr.empty())text+=(hasError?"Error. ":"Result. ")+accessibleMath(resultStr)+". ";
  if(select_all_active)text+="All input selected. ";
  text+=degreesMode?"Degrees.":"Radians.";return text;
 }
 case STATE_VARS:{
  if(var_create_mode||var_rename_mode)return "Variable name. "+accessibleMath(var_name_buf)+". "+String(var_err_str.c_str());
  if(var_edit_mode)return "Variable value. "+accessibleMath(var_edit_buf)+". "+String(var_err_str.c_str());
  if(user_args.empty())return "Variables. Empty. Control H reads commands.";
  size_t i=std::min(size_t(std::max(0,var_selected_idx)),user_args.size()-1);
  return "Variables. "+String(user_args[i].name.c_str())+" equals "+accessibleNumber(user_args[i].val)+". "+String(i+1)+" of "+String(user_args.size())+". Enter edits.";
 }
 case STATE_CONSTS:{
  if(const_create_mode||const_rename_mode)return "Constant name. "+accessibleMath(const_name_buf)+". "+String(const_err_str.c_str());
  if(const_edit_mode)return "Constant value. "+accessibleMath(const_edit_buf)+". "+String(const_err_str.c_str());
  if(user_consts.empty())return "Constants. Empty. Control H reads commands.";
  size_t i=std::min(size_t(std::max(0,const_selected_idx)),user_consts.size()-1);
  return "Constants. "+String(user_consts[i].name.c_str())+" equals "+accessibleNumber(user_consts[i].val)+". "+String(i+1)+" of "+String(user_consts.size())+". Enter edits.";
 }
 case STATE_FORMULAS:{
  if(formula_exit_prompt_mode)return String("Save formula changes? ")+(formula_exit_selected_idx==0?"Yes, save selected.":"No, discard selected.");
  if(formula_create_mode)return "New formula. "+accessibleMath(formula_create_buf)+". "+String(formula_err_str.c_str());
  if(formula_edit_mode)return "Edit formula. "+accessibleMath(formula_edit_buf)+". Cursor "+String(formula_cursor_pos+1)+". "+String(formula_err_str.c_str());
  if(user_formulas.empty())return "Formula wizards. Empty. Control H reads commands.";
  size_t i=std::min(size_t(std::max(0,formula_selected_idx)),user_formulas.size()-1);auto&f=user_formulas[i];
  if(formula_wizard_mode){
   size_t p=size_t(std::max(0,formula_wizard_param_idx));
   String result=formula_wizard_has_result?String("Last evaluated result. ")+accessibleMath(formula_wizard_result_str)+". ":String("");
   return "Formula "+String(f.name.c_str())+". "+result+"Parameter "+String(p+1)+" of "+String(f.params.size())+". "+(p<f.params.size()?String(f.params[p].c_str()):String("Complete"))+". Value "+(p<formula_wizard_bufs.size()&&!formula_wizard_bufs[p].empty()?accessibleMath(formula_wizard_bufs[p]):String("Empty"))+". Tab changes parameter. Enter evaluates all values and advances.";
  }
  return "Formula wizards. "+String(f.name.c_str())+". "+accessibleMath(f.expr)+". "+String(i+1)+" of "+String(user_formulas.size())+". Enter starts the wizard.";
 }
 case STATE_PARAMS:{
  const char*names[]={"Screen timeout, seconds","Brightness","Number format","Decimal precision","Thousands separator","Automatic brackets","Sticky modifiers","Block cursor"};
  int i=std::clamp(param_selected_idx,0,7);String value;
  const char*formats[]={"Normal","Float","Fixed","Scientific","Engineering"};
  switch(i){case 0:value=String(screen_off_timeout);break;case 1:value=String(backlight_brightness);break;case 2:value=formats[std::clamp(fmt_mode,0,4)];break;case 3:value=String(fmt_precision);break;case 4:value=use_thousands_sep?"On":"Off";break;case 5:value=auto_brackets?"On":"Off";break;case 6:value=sticky_mode?"On":"Off";break;default:value=block_cursor?"On":"Off";}
  return "Settings. "+String(names[i])+". "+(param_edit_mode?accessibleMath(param_edit_buf):value)+". "+String(i+1)+" of 8.";
 }
 case STATE_BINDS:{
  if(bind_edit_mode)return "Binding action. "+accessibleMath(bind_edit_buf);
  if(user_binds.empty())return "Keyboard bindings. Empty. Control H reads commands.";
  size_t i=std::min(size_t(std::max(0,bind_selected_idx)),user_binds.size()-1);
  return "Keyboard bindings. Key "+String(user_binds[i].key)+". Action "+accessibleMath(user_binds[i].action)+". "+String(i+1)+" of "+String(user_binds.size());
 }
 case STATE_SCRIPTS:{
  if(script_exit_prompt_mode)return String("Save script changes? ")+(script_exit_selected_idx==0?"Yes, save selected.":"No, discard selected.");
  if(script_name_prompt_mode)return "Script name. "+accessibleMath(script_name_edit_buf);
  if(script_edit_mode){int pos=std::clamp(script_cursor_pos,0,int(script_edit_buf.size()));int start=getLineStart(script_edit_buf,pos),end=getLineEnd(script_edit_buf,pos);return "Script editor. Line "+accessibleMath(script_edit_buf.substr(start,end-start))+". Column "+String(pos-start+1);}
  if(script_running_mode&&!script_console_output.empty()){
   accessibleConsole=std::min(accessibleConsole,script_console_output.size()-1);
   return "Script output. Line "+String(accessibleConsole+1)+" of "+String(script_console_output.size())+". "+accessibleMath(script_console_output[accessibleConsole])+". Control N or P reads next or previous output line.";
  }
  if(user_scripts.empty())return "Scripts. Empty. Control H reads commands.";
  size_t i=std::min(size_t(std::max(0,script_selected_idx)),user_scripts.size()-1);
  return "Scripts. "+String(user_scripts[i].name.c_str())+". "+String(i+1)+" of "+String(user_scripts.size())+". Control H reads commands.";
 }
 case STATE_PLOT:{
  if(plot_lines.empty())return "Plot data. No sampled curves. Expression "+accessibleMath(plot_expr)+". Control H reads plotting commands.";
  accessibleCurve=std::min(accessibleCurve,plot_lines.size()-1);auto&line=plot_lines[accessibleCurve];size_t count=std::min(line.x.size(),line.y.size());
  if(!count)return "Plot data. Selected curve has no points.";
  accessiblePoint=std::min(accessiblePoint,count-1);
  return "Plot data. Curve "+String(accessibleCurve+1)+" of "+String(plot_lines.size())+". Point "+String(accessiblePoint+1)+" of "+String(count)+". X "+accessibleNumber(line.x[accessiblePoint])+". Y "+accessibleNumber(line.y[accessiblePoint])+". Control N or P changes point. Control L changes curve.";
 }
 case STATE_HELP:{std::string title;std::vector<std::string> left,right;getHelpViewData(title,left,right);String text=title.c_str();for(auto&s:left)text+=". "+String(s.c_str());for(auto&s:right)text+=". "+String(s.c_str());return text;}
 }
 return "Utility ready.";
}
static void accessibleSpeakContext(){static uint32_t revision=0;String text=accessibleContext();Reader::focus("utility/"+String(++revision),text);Reader::context(text);}
static bool accessibleGlobalKey(Keyboard_Class::KeysState&s){
 char letter=s.word.empty()?0:char(tolower(s.word.front()));
 if(s.ctrl&&(s.space||letter==' ')){if(s.shift)Reader::next();else Reader::repeat();return true;}
 if(s.ctrl&&s.backspace){Reader::stop();return true;}
 if(s.ctrl&&letter=='o'){accessibleMenu=!accessibleMenu;show_help_popup=false;Reader::stop();accessibleSpeakContext();return true;}
 if(accessibleMenu){
  bool up=s.up||letter==';',down=s.down||letter=='.';
  if(up)accessibleMenuIndex=(accessibleMenuIndex+7)%8;if(down)accessibleMenuIndex=(accessibleMenuIndex+1)%8;
  if(s.esc||letter=='`'||s.backspace){accessibleMenu=false;Reader::stop();}
  else if(s.enter){accessibleMenu=false;resetAppState(accessibleModes[accessibleMenuIndex]);}
  accessibleSpeakContext();return true;
 }
 if(s.ctrl&&letter=='h'){show_help_popup=!show_help_popup;Reader::stop();if(show_help_popup)Reader::event(accessibleContext());else accessibleSpeakContext();return true;}
 if(show_help_popup){
  if(s.esc||s.enter||s.backspace||letter=='`'){show_help_popup=false;Reader::stop();accessibleSpeakContext();}
  else if(s.up||s.down||letter==';'||letter=='.'){help_popup_scroll_offset=std::max(0,help_popup_scroll_offset+((s.up||letter==';')?-1:1));Reader::event(accessibleContext());}
  return true;
 }
 if(appState==STATE_PLOT&&s.ctrl&&(letter=='n'||letter=='p'||letter=='l')){
  if(letter=='n')++accessiblePoint;else if(letter=='p'&&accessiblePoint)--accessiblePoint;
  else if(letter=='l'&&!plot_lines.empty()){accessibleCurve=(accessibleCurve+1)%plot_lines.size();accessiblePoint=0;}
  accessibleSpeakContext();return true;
 }
 if(appState==STATE_SCRIPTS&&script_running_mode&&s.ctrl&&(letter=='n'||letter=='p')){
  if(letter=='n')++accessibleConsole;else if(accessibleConsole)--accessibleConsole;accessibleSpeakContext();return true;
 }
 return false;
}
static void accessibleAnnounce(const Keyboard_Class::KeysState&s,AppState before){
 static uint32_t revision=0;
 if(show_help_popup){Reader::event(accessibleContext());return;}
 if(appState==STATE_CALC&&before==STATE_CALC&&s.enter){Reader::event(resultStr.empty()?accessibleContext():(hasError?String("Error. "):String("Result. "))+accessibleMath(resultStr));Reader::context(accessibleContext());return;}
 if(before!=appState||show_help_popup||s.enter||s.tab||s.up||s.down||s.left||s.right||s.esc||s.backspace||s.del||s.ctrl||s.fn){accessibleSpeakContext();return;}
 if(!s.word.empty()){
  std::string typed(s.word.begin(),s.word.end());Reader::focus("typed/"+String(++revision),typed==" "?String("Space"):accessibleMath(typed));Reader::context(accessibleContext());
 }
}
