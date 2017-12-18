#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "ChatScreen.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/AmultiosChatClient.h"
#include "i18n/i18n.h"
#include <ctype.h>
#include "util/text/utf8.h"
#include "base/timeutil.h"

std::string chatTo = "All";
int chatGuiIndex = 0;

ChatScreen::ChatScreen() {
	alpha_ = 0.0f;
}

void ChatScreen::CreateViews() {
	using namespace UI;

	I18NCategory *n = GetI18NCategory("Networking");
	UIContext &dc = *screenManager()->getUIContext();

	AnchorLayout *anchor = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	anchor->Overflow(false);
	root_ = anchor;

	float yres = screenManager()->getUIContext()->GetBounds().h;

	const int cy = 290;
	const int cx = 200;

	switch (g_Config.iChatScreenPosition) {
	// the chat screen size is still static 280,250 need a dynamic size based on device resolution 
	case 0:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight() , cy, NONE, NONE, cx, true));
		break;
	case 1:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), dc.GetBounds().centerX(), NONE, NONE, cx, true));
		break;
	case 2:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), NONE, NONE, cy, cx, true));
		break;
	case 3:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), cy, cx, NONE, NONE, true));
		break;
	case 4:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), dc.GetBounds().centerX(), cx, NONE, NONE, true));
		break;
	case 5:
		box_ = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(ChatScreenWidth(), ChatScreenHeight(), NONE, cx, cy, NONE, true));
		break;
	}

	root_->Add(box_);
	box_->SetBG(UI::Drawable(0x00303030));
	box_->SetHasDropShadow(false);

	UI::ChoiceDynamicValue *channel = new ChoiceDynamicValue(&chatTo,new LinearLayoutParams(110,50));
	channel->OnClick.Handle(this, &ChatScreen::OnChangeChannel);

	scroll_ = box_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0)));
	scroll_->setBobColor(0x99303030);
	chatVert_ = scroll_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	chatVert_->SetSpacing(0);

	LinearLayout *bottom = box_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(ChatScreenWidth(), WRAP_CONTENT)));
	bottom->Add(channel);
	
#if defined(_WIN32) || defined(USING_QT_UI)
	chatEdit_ = bottom->Add(new TextEdit("", n->T("Chat Here"), new LinearLayoutParams((ChatScreenWidth() -120),50)));
	chatEdit_->SetMaxLen(63);
	chatEdit_->OnEnter.Handle(this, &ChatScreen::OnSubmit);
	UI::EnableFocusMovement(true);
	root_->SetDefaultFocusView(chatEdit_);
	root_->SetFocus();
#if defined(USING_WIN_UI)
	//freeze  the ui when using ctrl + C hotkey need workaround
	if (g_Config.bBypassOSKWithKeyboard && !g_Config.bFullScreen)
	{
		std::wstring titleText = ConvertUTF8ToWString(n->T("Chat"));
		std::wstring defaultText = ConvertUTF8ToWString(n->T("Chat Here"));
		std::wstring inputChars;
		if (System_InputBoxGetWString(titleText.c_str(), defaultText, inputChars)) {
			//chatEdit_->SetText(ConvertWStringToUTF8(inputChars));
			sendChat(ConvertWStringToUTF8(inputChars));
		}
	}
#endif
#elif defined(__ANDROID__)
	bottom->Add(new Button(n->T("Chat Here"), new LayoutParams((550 - 120), 50)))->OnClick.Handle(this, &ChatScreen::OnSubmit);
#endif
	cmList.toogleChatScreen(true);
	UpdateChat();
}

void ChatScreen::dialogFinished(const Screen *dialog, DialogResult result) {
	UpdateUIState(UISTATE_INGAME);
}

UI::EventReturn ChatScreen::OnSubmit(UI::EventParams &e) {
#if defined(_WIN32) || defined(USING_QT_UI)
	std::string chat = chatEdit_->GetText();
	chatEdit_->SetText("");
	chatEdit_->SetFocus();
	sendChat(chat);
#elif defined(__ANDROID__)
	System_SendMessage("inputbox", "Chat:");
#endif
	return UI::EVENT_DONE;
}

UI::EventReturn ChatScreen::OnChangeChannel(UI::EventParams &params) {

	chatGuiIndex += 1;
	if (chatGuiIndex >= 2) {
		chatGuiIndex = 0;
	}

	switch (chatGuiIndex)
	{
	case 0:
		chatTo = "All";
		chatGuiStatus = CHAT_GUI_ALL;
		break;
	case 1:
		chatTo = "Group";
		chatGuiStatus = CHAT_GUI_GROUP;
		break;
	//case 2:
		//chatTo = "Server";
		//chatGuiStatus = CHAT_GUI_SERVER;
		//break;
	//case 3:
		//chatTo = "Game";
		//chatGuiStatus = CHAT_GUI_GAME;
		//break;
	default:
		chatTo = "All";
		chatGuiStatus = CHAT_GUI_ALL;
		break;
	}
	UpdateChat();
	return UI::EVENT_DONE;
}

/*
	maximum chat length in one message from server is only 64 character
	need to split the chat to fit the static chat screen size
	if the chat screen size become dynamic from device resolution
	we need to change split function logic also.
*/

void ChatScreen::UpdateChat() {
	using namespace UI;
	
	if (chatVert_ != nullptr) {
		chatVert_->Clear();
		cmList.Lock();
		const std::list<ChatMessages::ChatMessage> &messages = cmList.Messages(chatGuiStatus);
		for (auto iter = messages.begin(); iter != messages.end(); ++iter) {
			if (iter->name == "" || iter->onlytext) {
				TextView *v = chatVert_->Add(new TextView(iter->text, FLAG_DYNAMIC_ASCII, true));
				v->SetTextColor(0xFF000000 | iter->textcolor);
			}
			else {
				LinearLayout *line = chatVert_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT)));
				if (chatGuiStatus == CHAT_GUI_ALL) {
					if (iter->room != "") {
						TextView *GroupView = line->Add(new TextView(iter->room, FLAG_DYNAMIC_ASCII, true));
						GroupView->SetTextColor(0xFF000000 | iter->roomcolor);
					}
					TextView *nameView = line->Add(new TextView(iter->name, FLAG_DYNAMIC_ASCII, true));
					nameView->SetTextColor(0xFF000000 | iter->namecolor);
				}
				else {
					TextView *nameView = line->Add(new TextView(iter->name, FLAG_DYNAMIC_ASCII, true));
					nameView->SetTextColor(0xFF000000 | iter->namecolor);
					nameView->SetShadow(true);
				}
				TextView *chatView = line->Add(new TextView(iter->text, FLAG_DYNAMIC_ASCII, true));
				chatView->SetTextColor(0xFF000000 | iter->textcolor);
			}
		}
		cmList.Unlock();
		toBottom_ = true;
	}
}

bool ChatScreen::touch(const TouchInput &touch) {

	if (!box_ || (touch.flags & TOUCH_DOWN) == 0 || touch.id != 0) {
		return UIDialogScreen::touch(touch);
	}

	if (!box_->GetBounds().Contains(touch.x, touch.y)){
		screenManager()->finishDialog(this, DR_BACK);
	}

	return UIDialogScreen::touch(touch);
}

void ChatScreen::update() {
	UIDialogScreen::update();

	alpha_ = 1.0f;

	if (scroll_ && toBottom_) {
		toBottom_ = false;
		scroll_->ScrollToBottom();
	}

	const float now = time_now();
	if (now > cmList.getLastUpdate() && cmList.getChatUpdate()) {
		UpdateChat();
		cmList.doChatUpdate();
	}
}


ChatScreen::~ChatScreen() {
	cmList.toogleChatScreen(false);
}
