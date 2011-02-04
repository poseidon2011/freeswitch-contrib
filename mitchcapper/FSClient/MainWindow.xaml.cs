﻿using System;
using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using FSClient.Controls;

namespace FSClient {
	public partial class MainWindow : Window {
		private Broker broker;
		public MainWindow() {
			_instance = this;

			InitializeComponent();

			gridCalls.DataContext = Call.calls;


			gridCalls.LoadingRow += gridCalls_LoadingRow;
			gridAccounts.DataContext = Account.accounts;

			this.Loaded += MainWindow_Loaded;


		}

		private void ActiveCallChanged(object sender, Call.ActiveCallChangedArgs e) {
			Dispatcher.BeginInvoke((Action)(() => {
				CurrentCallInfo.DataContext = Call.active_call;
				if (Call.active_call == null)
					CurrentCallInfo.Visibility = Visibility.Hidden;
				else
					CurrentCallInfo.Visibility = Visibility.Visible;
			}));
		}
		private void CallStateChanged(object sender, Call.CallPropertyEventArgs e) {
			gridCalls.Items.SortDescriptions.Clear();
			gridCalls.Items.SortDescriptions.Add(new SortDescription("sort_order", ListSortDirection.Descending));
		}
		void accounts_CollectionChanged(object sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e) {
			if (e.NewItems == null)
				return;
			foreach (Account acct in e.NewItems) {
				acct.PropertyChanged += acct_PropertyChanged;
			}
		}

		void acct_PropertyChanged(object sender, PropertyChangedEventArgs e) {
			if (e.PropertyName == "gateway_id") {
				gridAccounts.Items.SortDescriptions.Clear();
				gridAccounts.Items.SortDescriptions.Add(new SortDescription("gateway_id", ListSortDirection.Ascending));
			}
		}




		void gridCalls_LoadingRow(object sender, DataGridRowEventArgs e) {
			e.Row.SetResourceReference(ToolTipProperty, "mainCallTooltip");
			Call c = e.Row.DataContext as Call;
			if (c == null)
				return;
			e.Row.ContextMenu = c.CallRightClickMenu();

		}
		public void SetDialStr(string str) {
			Dispatcher.BeginInvoke((Action)(() => {
				txtNumber.Text = str;
			}));
		}

		private void CanEndChanged(object sender, bool data) {
			Dispatcher.BeginInvoke((Action)(() => {
				btnHangup.IsEnabled = broker.CanEnd;
			}));
		}
		private void SpeakerActiveChanged(object sender, bool data) {
			Dispatcher.BeginInvoke((Action)(() => {
				btnSpeaker.Foreground = data ? enabled_brush : disabled_brush;
			}));
		}
		private void MuteChanged(object sender, bool data) {
			Dispatcher.BeginInvoke((Action)(() => {
				btnMute.Foreground = data ? enabled_brush : disabled_brush;
			}));
		}
		private void DNDChanged(object sender, bool data) {
			Dispatcher.BeginInvoke((Action)(() => {
				btnDND.Foreground = data ? enabled_brush : disabled_brush;
				Title = "FSClient " + (data ? " - DND" : "");
			}));
		}
		private void CallActiveChanged(object sender, bool data) {
			Dispatcher.BeginInvoke((Action)(() => {
				btnHold.IsEnabled = broker.call_active;
				btnTransfer.IsEnabled = broker.call_active;
			}));

		}



		#region TextInput

		private enum TEXT_INPUT_MODE { NUMBERS_ONLY, FULL };

		private TEXT_INPUT_MODE text_mode = TEXT_INPUT_MODE.NUMBERS_ONLY;
		private void simple_text_mode_char_convert(char c) {
			if (c == '*' || c == '#' || (c >= '0' && c <= '9')) {
				broker.handle_key_action(c);
				return;
			}
			switch (Char.ToUpper(c)) {
				case 'A':
				case 'B':
				case 'C':
					broker.handle_key_action('2');
					break;
				case 'D':
				case 'E':
				case 'F':
					broker.handle_key_action('3');
					break;
				case 'G':
				case 'H':
				case 'I':
					broker.handle_key_action('4');
					break;
				case 'J':
				case 'K':
				case 'L':
					broker.handle_key_action('5');
					break;
				case 'M':
				case 'N':
				case 'O':
					broker.handle_key_action('6');
					break;
				case 'P':
				case 'Q':
				case 'R':
				case 'S':
					broker.handle_key_action('7');
					break;
				case 'T':
				case 'U':
				case 'V':
					broker.handle_key_action('8');
					break;
				case 'W':
				case 'X':
				case 'Y':
				case 'Z':
					broker.handle_key_action('9');
					break;
			}
		}

		private bool text_interception_enabled = true;
		void MainWindow_PreviewTextInput(object sender, TextCompositionEventArgs e) {
			if (!broker.fully_loaded)
				return;
			if (!text_interception_enabled) {
				e.Handled = false;
				return;
			}
			char[] chars = e.Text.ToCharArray();
			char[] sys_chars = e.ControlText.ToCharArray();
			foreach (Char c in sys_chars) {
				if (c == 22) //paste / control + v
				{
					String clipboard = Clipboard.GetText();
					char[] tmp = new char[chars.Length + clipboard.Length];
					chars.CopyTo(tmp, 0);
					clipboard.ToCharArray().CopyTo(tmp, chars.Length);
					chars = tmp;
				}
				else if (c == 6){ // control + f
					txtSearchBox.TextBoxFocus();
				}
			}

			e.Handled = false;
			foreach (Char c in chars) {
				bool handled = true;
				if (c > 32 && c < 127) {
					if (text_mode == TEXT_INPUT_MODE.NUMBERS_ONLY || (Call.active_call != null && Call.active_call.state == Call.CALL_STATE.Answered))
						simple_text_mode_char_convert(c);
					else
						broker.handle_key_action(c);

				}
				else if (c == '\b')
					broker.handle_special_action(Broker.KEYBOARD_ACTION.Backspace);
				else if (c == '\r')
					broker.handle_special_action(Broker.KEYBOARD_ACTION.Enter);
				else if (c == 27)
					broker.handle_special_action(Broker.KEYBOARD_ACTION.Escape);
				else
					handled = false;
				if (handled)
					e.Handled = true;
			}
		}
		void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e) {
			if (!broker.fully_loaded)
				return;
			if (!text_interception_enabled) {
				e.Handled = false;
				return;
			}

			if (e.Key == Key.Return) {
				broker.handle_special_action(Broker.KEYBOARD_ACTION.Enter);
				e.Handled = true;
			}
		}
		#endregion
		private static MainWindow _instance;
		public static MainWindow get_instance() {
			return _instance;
		}

		private void DialStrChanged(object sender, string data) {
			SetDialStr(broker.cur_dial_str);
		}
		private void CallRingingChanged(object sender, bool data) {
			Dispatcher.Invoke((Action)(() => {
				if (data && Call.active_call != null && Call.active_call.CanSendToVoicemail())
					btnSendVoicemail.Visibility = Visibility.Visible;
				else
					btnSendVoicemail.Visibility = Visibility.Hidden;
			}));
		}
		void MainWindow_Loaded(object sender, RoutedEventArgs e) {
			PreviewTextInput += MainWindow_PreviewTextInput;
			PreviewKeyDown += MainWindow_PreviewKeyDown; //return must be handled seperately as buttons are triggered on down it seems
			MouseUp += MainWindow_MouseUp;

			Call.CallStateChanged += CallStateChanged;
			Call.ActiveCallChanged += ActiveCallChanged;
			Account.accounts.CollectionChanged += accounts_CollectionChanged;
			Broker.FreeswitchLoaded += FreeswitchLoaded;
			broker = Broker.get_instance();

			broker.cur_dial_strChanged += DialStrChanged;
			broker.call_activeChanged += CallActiveChanged;
			broker.active_call_ringingChanged += CallRingingChanged;
			broker.MutedChanged += MuteChanged;
			broker.DNDChanged += DNDChanged;
			broker.CanEndChanged += CanEndChanged;
			broker.UseNumberOnlyInputChanged += UseNumberOnlyInputChanged;
			UseNumberOnlyInputChanged(null, false);//trigger an update
			broker.SpeakerphoneActiveChanged += SpeakerActiveChanged;
			CurrentCallInfo.Visibility = Visibility.Hidden;
			Windows.systray_icon_setup();
		}

		void MainWindow_MouseUp(object sender, MouseButtonEventArgs e) {
			if (txtSearchBox.TextBoxHasFocus()){
				DependencyObject parent = e.OriginalSource as UIElement;
				while (parent != null && !(parent is OurAutoCompleteBox)) 
					parent = VisualTreeHelper.GetParent(parent);
				if (parent == null)
					RemoveFocus();
			}

		}

		public void RemoveFocus(bool ResetContactSearchText=false){
			btnMute.Focus(); //should really divert focus a better way
			if (ResetContactSearchText)
				ResetContactSearchStr();
		}



		private void FreeswitchLoaded(object sender, EventArgs e) {
			Dispatcher.BeginInvoke((Action)(() => {
				busyAnimation.Visibility = Visibility.Hidden;
			}));
		}



		private void UseNumberOnlyInputChanged(object sender, bool data) {
			text_mode = broker.UseNumberOnlyInput ? TEXT_INPUT_MODE.NUMBERS_ONLY : TEXT_INPUT_MODE.FULL;
		}

		public void BringToFront() {
			Show();
			BringIntoView();
			WindowState = WindowState.Normal;
			Topmost = true;
			Topmost = false;
		}
		private void btnCall_Click(object sender, RoutedEventArgs e) {
			broker.TalkPressed();

		}

		private void btnOptions_Click(object sender, RoutedEventArgs e) {
			Options opt = new Options();
			opt.ShowDialog();
		}

		private void btnHangup_Click(object sender, RoutedEventArgs e) {
			broker.HangupPressed();
		}

		private void gridCalls_MouseDoubleClick(object sender, MouseButtonEventArgs e) {
			FrameworkElement elem = e.OriginalSource as FrameworkElement;
			if (elem == null)
				return;
			Call call = elem.DataContext as Call;
			if (call == null)
				return;
			call.DefaultAction();
		}

		SolidColorBrush enabled_brush = new SolidColorBrush(Colors.Yellow);
		SolidColorBrush disabled_brush = new SolidColorBrush(Colors.White);
		private void btnMute_Click(object sender, RoutedEventArgs e) {
			broker.Muted = !broker.Muted;
		}

		private void btnDND_Click(object sender, RoutedEventArgs e) {
			broker.DND = !broker.DND;
		}
		private void gridAccounts_MouseUp(object sender, MouseButtonEventArgs e) {
			FrameworkElement elem = e.OriginalSource as FrameworkElement;
			if (elem == null)
				return;
			Account account = elem.DataContext as Account;
			if (account == null)
				return;
			DependencyObject dep = elem;
			while (dep != null && !(dep is DataGridCell)) {
				dep = VisualTreeHelper.GetParent(dep);
			}
			DataGridCell cell = dep as DataGridCell;
			if (cell == null)
				return;

			if (cell.Column.SortMemberPath == "enabled") {
				account.enabled = !account.enabled;
				account.ReloadAccount();
			}
		}

		private void Window_Closing(object sender, CancelEventArgs e) {
			try {
				Windows.systray_icon_remove();
				broker.Dispose();
			}
			catch{ }
		}

		private void btnSpeaker_Click(object sender, RoutedEventArgs e) {
			broker.SpeakerphoneActive = !broker.SpeakerphoneActive;
		}


		private void AccountNew_Click(object sender, RoutedEventArgs e) {
			Account acct = new Account();
			Account.AddAccount(acct);
			if (!acct.edit())
				Account.RemoveAccount(acct);

		}

		private void AccountEdit_Click(object sender, RoutedEventArgs e) {
			Account acct = gridAccounts.SelectedItem as Account;
			if (acct == null)
				return;
			acct.edit();
		}

		private void AccountSetDefault_Click(object sender, RoutedEventArgs e) {
			Account acct = gridAccounts.SelectedItem as Account;
			if (acct == null)
				return;
			acct.is_default_account = true;
		}

		private void AccountDelete_Click(object sender, RoutedEventArgs e) {
			Account acct = gridAccounts.SelectedItem as Account;
			if (acct == null)
				return;
			Account.RemoveAccount(acct);
		}


		private void gridAccounts_ContextMenuOpening(object sender, ContextMenuEventArgs e) {
			menuAccountDelete.IsEnabled = menuAccountEdit.IsEnabled = menuAccountSetDefault.IsEnabled = gridAccounts.SelectedItem != null;
		}

		private void btnDialpad_Click(object sender, RoutedEventArgs e) {
			Button btn = sender as Button;
			if (btn != null)
				broker.handle_key_action(btn.Content.ToString()[0]);
			PhonePadButton btn2 = sender as PhonePadButton;
			if (btn2 != null)
				broker.handle_key_action(btn2.Number[0]);

		}

		private void btnHold_Click(object sender, RoutedEventArgs e) {
			if (Call.active_call != null)
				Call.active_call.hold();
		}

		private void btnSendVoicemail_Click(object sender, RoutedEventArgs e) {
			if (Call.active_call != null)
				Call.active_call.SendToVoicemail();
		}

		private void btnTransfer_Click(object sender, RoutedEventArgs e) {
			if (Call.active_call != null)
				Call.active_call.TransferPrompt();


		}
		#region ContactSearchBox
		public OurAutoCompleteBox GetContactSearchBox() {
			return txtSearchBox;
		}

		private const string contact_search_text = "Contact Search";
		private void txtSearchBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) {
			text_interception_enabled = false;
			txtSearchBox.Opacity = 1;
			if (txtSearchBox.Text == contact_search_text)
				txtSearchBox.Text = "";
		}

		private void txtSearchBox_LostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) {
			text_interception_enabled = true;
			txtSearchBox.Opacity = 0.8;

			if (String.IsNullOrWhiteSpace(txtSearchBox.Text))
				ResetContactSearchStr();
		}
		public void ResetContactSearchStr(){
			txtSearchBox.Text = contact_search_text;
		}

		private bool ContactMenuOpen;
		private void contactSearchConextMenu_Closed(object sender, RoutedEventArgs e) {
			ContactMenuOpen = false;
			txtSearchBox.IsDropDownOpen = false;
		}

		private void contactSearchConextMenu_Loaded(object sender, RoutedEventArgs e) {
			ContactMenuOpen = true;
		}

		private void txtSearchBox_DropDownClosing(object sender, RoutedPropertyChangingEventArgs<bool> e) {
			if (ContactMenuOpen)
				e.Cancel = true;
		}
		#endregion
	}
}