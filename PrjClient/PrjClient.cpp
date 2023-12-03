#pragma comment(lib, "ws2_32")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "resource.h"

#define SERVERIPV4  "127.0.0.1"
#define SERVERIPV6  "::1"
#define SERVERPORT  9000

#define CHATTING    1000                         // �޽��� Ÿ��: ä��
#define DRAWLINE    1001                         // �޽��� Ÿ��: �׸� �׸���
#define WHISPER     1002                         // �޽��� Ÿ��: �ӼӸ�
#define WARNING     1003                         // �޽��� Ÿ��: ���
#define NOTICE      1004                         // �޽��� Ÿ��: ����(��������, �׸��� �ʱ�ȭ)
#define FILESEND    1005                         // �޽��� Ÿ��: ���� ������
#define FILENAME    1006                         // �޽��� Ÿ��: ���� �̸�

#define CIRCLE_MODE 1
#define RECT_MODE   2
#define PENCIL_MODE 3

#define WM_DRAWIT   (WM_USER+1)                  // ����� ���� ������ �޽���
#define CONNECTION_SUCCESS 0                     // ���� �ڵ�: ����
#define CONNECTION_FAILED_NICKNAME_DUPLICATED 1  // ���� �ڵ�: �г��� �ߺ�(����)

struct DRAWLINE_MSG                              // �׸� ����ü(����, ��ǥ, ����)
{
	int  color;
	int  x0, y0;
	int  x1, y1;
	int  width;
	int drawmode;
};

static HINSTANCE     g_hInst; // ���� ���α׷� �ν��Ͻ� �ڵ�
static HWND          g_hDrawWnd; // �׸��� �׸� ������
static HWND          g_hButtonSendMsg; // '�޽��� ����' ��ư
static HWND          g_hEditStatus; // ���� �޽��� ���
static char          g_ipaddr[64]; // ���� IP �ּ�
static u_short       g_port; // ���� ��Ʈ ��ȣ
static char          g_userid[128]; // ����� ID
static BOOL          g_isIPv6; // IPv4 or IPv6 �ּ�?
static HANDLE        g_hClientThread; // ������ �ڵ�
static int           g_bStart; // ��� ���� ����
static SOCKET        g_sock; // Ŭ���̾�Ʈ ����
static HANDLE        g_hReadEvent, g_hWriteEvent; // �̺�Ʈ �ڵ�
static char*         g_chatmsg; // ä�� �޽��� ����
static int           g_chatmsglen; // ä�� �޽��� ����
static DRAWLINE_MSG  g_drawmsg; // �� �׸��� �޽��� ����
static int           g_drawcolor = RGB(255,0,0); // �� �׸��� ����
static int           g_drawcolororiginal = RGB(255,0,0); // �� �׸��� ����
static int           g_drawwidth = 3;     // �� �׸��� ����
static BOOL          g_isUDP;             // UDP���� �ƴ���
static BOOL          g_isErase;      // ���찳 ���
static HWND          g_chooseColorButton; // ���� ���� ��ư
static HWND          hWidthSelect;        // ���� ���� �޺��ڽ�
static HWND			 hPencilButton;       // ���� ��ư
static HWND			 hRectButton;         // �簢�� ��ư
static HWND			 hCircleButton;       // �� ��ư
static HWND			 hEraseButton;        // ���찳 ��ư
static HDC			 hDC;                 // �׸��� �ڵ鷯
static int           g_drawmode = PENCIL_MODE;          // �׸��� ���
static int           type;                // �޽��� Ÿ��
static int           g_drawmsgsize = sizeof(DRAWLINE_MSG);
static HWND          hFileName;           // ���� �̸� �ؽ�Ʈ
static HWND          hFileSelectButton;   // ���� ���� ��ư
static HWND          hFileSendButton;     // ���� ���� ��ư
static char          g_filename[1024];     // ���� �̸� ���ڿ�
static int           g_filenamelen;       // ���� �̸� ����
static char          *g_receivedfilename; // ������ ���� �̸�

// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// �ڽ� ������ ���ν���
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
// ���� ��Ʈ�� ��� �Լ�
void DisplayText(char* fmt, ...);
// ����� ���� ������ ���� �Լ�
int recvn(SOCKET s, char* buf, int len, int flags);
// ���� ��� �Լ�
void err_quit(char* msg);
void err_display(char* msg);
// ���� �ñ״�ó �о���̴� �Լ�
char* get_file_signature(char* filename);

// ���� �Լ�
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// ���� �ʱ�ȭ
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// �̺�Ʈ ����(ReadEvent : �޽��� ����, WriteEvent : �޽��� �۽� �̺�Ʈ)
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// ���� �ʱ�ȭ(�Ϻ�)
	g_drawmsg.color = RGB(255, 0, 0);
	g_drawmsg.width = 3;
	g_drawmsg.drawmode = PENCIL_MODE;

	// ��ȭ���� ����
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);


	// �̺�Ʈ ����
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// ���� ����
	WSACleanup();
	return 0;
}

// ��ȭ���� ���ν���
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hButtonIsIPv6;
	static HWND hEditIPaddr;
	static HWND hEditPort;
	static HWND hEditUserID;
	static HWND hButtonConnect;
	static HWND hEditMsg;
	static HWND hButtonIsUDP;

	switch (uMsg) {
	// â �ʱ�ȭ
	case WM_INITDIALOG:
		// ��Ʈ�� �ڵ� ���
		hButtonIsIPv6 = GetDlgItem(hDlg, IDC_ISIPV6);
		hEditIPaddr = GetDlgItem(hDlg, IDC_IPADDR);
		hEditPort = GetDlgItem(hDlg, IDC_PORT);
		hEditUserID = GetDlgItem(hDlg, IDC_USERID);
		hButtonConnect = GetDlgItem(hDlg, IDC_CONNECT);
		g_hButtonSendMsg = GetDlgItem(hDlg, IDC_SENDMSG);
		hEditMsg = GetDlgItem(hDlg, IDC_MSG);
		g_hEditStatus = GetDlgItem(hDlg, IDC_STATUS);
		hButtonIsUDP = GetDlgItem(hDlg, IDC_ISUDP);
		g_chooseColorButton = GetDlgItem(hDlg, IDC_CHOOSECOLOR);
		hWidthSelect = GetDlgItem(hDlg, IDC_COMBO1);
		hPencilButton = GetDlgItem(hDlg, IDC_PENCILBUTTON);
		hRectButton = GetDlgItem(hDlg, IDC_RECTBUTTON);
		hCircleButton = GetDlgItem(hDlg, IDC_CIRCLEBUTTON);
		hEraseButton = GetDlgItem(hDlg, IDC_ERASEBUTTON);
		hFileName = GetDlgItem(hDlg, IDC_FILENAME);
		hFileSelectButton = GetDlgItem(hDlg, IDC_CHOOSEFILE);
		hFileSendButton = GetDlgItem(hDlg, IDC_SENDFILE);

		// ��Ʈ�� �ʱ�ȭ(��ư ��Ȱ��ȭ, �ʱⰪ ����)
		EnableWindow(g_chooseColorButton, FALSE);
		EnableWindow(hWidthSelect, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
		SendMessage(hPencilButton, BM_SETCHECK, BST_CHECKED, 0);

		// ������ Ŭ���� ���
		WNDCLASS wndclass;
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = WndProc;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;
		wndclass.hInstance = g_hInst;
		wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName = NULL;
		wndclass.lpszClassName = "MyWndClass";
		if (!RegisterClass(&wndclass)) return 1;

		// �ڽ� ������ ����(�׸���)
		g_hDrawWnd = CreateWindow("MyWndClass", "�׸� �׸� ������", WS_CHILD,
			450, 38, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
		if (g_hDrawWnd == NULL) return 1;
		ShowWindow(g_hDrawWnd, SW_SHOW);
		UpdateWindow(g_hDrawWnd);

		// �� ���� �׸� �߰�
		for (int i = 1; i <= 10; i++) {
			char width[3];
			SendMessage(hWidthSelect, CB_ADDSTRING, 0, (LPARAM)TEXT(itoa(i, width, 10)));
		}
		return TRUE;

	// �ڵ鷯���� �̺�Ʈ�� �޾��� ��
	case WM_COMMAND:
		if (HIWORD(wParam) == BN_CLICKED) {
			switch (LOWORD(wParam)) {
			case IDC_CIRCLEBUTTON:
				g_drawmsg.color = g_drawcolororiginal;
				g_drawmsg.drawmode = CIRCLE_MODE;
				return TRUE;
			case IDC_RECTBUTTON:
				g_drawmsg.color = g_drawcolororiginal;
				g_drawmsg.drawmode = RECT_MODE;
				return TRUE;
			case IDC_PENCILBUTTON:
				g_drawmsg.color = g_drawcolororiginal;
				g_drawmsg.drawmode = PENCIL_MODE;
				return TRUE;
			case IDC_ERASEBUTTON:
				g_drawcolororiginal = g_drawmsg.color;
				g_drawmsg.color = RGB(255, 255, 255);
				g_drawmsg.drawmode = PENCIL_MODE;
				return TRUE;
			}
		}
		// ���� ���� �̺�Ʈ
		if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO1)
		{
			HWND hComboBox = (HWND)lParam;
			int index = SendMessage(hComboBox, CB_GETCURSEL, 0, 0);

			TCHAR text[3];
			SendMessage(hComboBox, CB_GETLBTEXT, index, (LPARAM)text);

			g_drawmsg.width = atoi(text);
			return TRUE;
		}

		switch (LOWORD(wParam)) {
		// IPv6 üũ�ڽ� �̺�Ʈ
		case IDC_ISIPV6:
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			if (g_isIPv6 == false)
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
			else
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
			return TRUE;

		// UDP üũ�ڽ� �̺�Ʈ
		case IDC_ISUDP:
			g_isUDP = SendMessage(hButtonIsUDP, BM_GETCHECK, 0, 0);
			return TRUE;

		// ���󺯰� �̺�Ʈ
		case IDC_CHOOSECOLOR:
		{
			CHOOSECOLOR cc = { 0 };
			COLORREF custColors[16] = { 0 };

			cc.lStructSize = sizeof(CHOOSECOLOR);
			cc.hwndOwner = NULL;
			cc.lpCustColors = custColors;
			cc.Flags = CC_RGBINIT | CC_FULLOPEN;

			if (ChooseColor(&cc)) {
				int red = GetRValue(cc.rgbResult);
				int green = GetGValue(cc.rgbResult);
				int blue = GetBValue(cc.rgbResult);
				g_drawcolororiginal = RGB(red, green, blue);
				g_drawmsg.color = RGB(red, green, blue);
			}	
			return TRUE;
		}
		case IDC_CHOOSEFILE:
		{
			memset(g_filename, 0, sizeof(g_filename));
			OPENFILENAME ofn;
			ZeroMemory(&ofn, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = NULL;
			ofn.lpstrFile = g_filename;
			ofn.nMaxFile = sizeof(g_filename) / sizeof(*g_filename);
			ofn.lpstrFilter = TEXT("��� ����(*.*)\0*.*\0");
			ofn.nFilterIndex = 1;
			ofn.lpstrFileTitle = NULL;
			ofn.nMaxFileTitle = 0;
			ofn.lpstrInitialDir = NULL;
			ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

			if (GetOpenFileName(&ofn) == TRUE)
			{
				SetDlgItemText(hDlg, IDC_FILENAME, strrchr(g_filename, '\\')+1);
			}

			return TRUE;
		}

		case IDC_SENDFILE:
		{
			int retval;

			type = FILENAME;
			g_chatmsglen = strlen(strrchr(g_filename, '\\')+1);
			g_chatmsg = (char*)malloc(g_chatmsglen + 1);
			strcpy(g_chatmsg, strrchr(g_filename, '\\') + 1);
			SetEvent(g_hWriteEvent);

			Sleep(10);

			type = FILESEND;
			FILE* fp = fopen(g_filename, "rb");
			if (fp != NULL) {
				fseek(fp, 0, SEEK_END);
				g_chatmsglen = ftell(fp);
				fseek(fp, 0, SEEK_SET);
				g_chatmsg = (char*)malloc(g_chatmsglen + 1);
				retval = fread(g_chatmsg, sizeof(char), g_chatmsglen, fp);
				fclose(fp);

				SetEvent(g_hWriteEvent);
			}
			else {
				printf("���� ���� ����\n");
			}
			return TRUE;
		}

		// ���� ���� ��ư �̺�Ʈ
		case IDC_CONNECT:
			GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
			GetDlgItemText(hDlg, IDC_USERID, g_userid, sizeof(g_userid));
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);

			if (g_port == 0) {
				MessageBox(hDlg, "��Ʈ�� �Է����ּ���.", "����!", MB_ICONERROR);
				return FALSE;
			}

			if (strlen(g_ipaddr) == 0) {
				MessageBox(hDlg, "IP �ּҸ� �Է����ּ���.", "����!", MB_ICONERROR);
				return FALSE;
			}

			if (strlen(g_userid) == 0) {
				MessageBox(hDlg, "���̵� �Է����ּ���.", "����!", MB_ICONERROR);
				return FALSE;
			}

			// ���� ��� ������ ����
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) {
				MessageBox(hDlg, "Ŭ���̾�Ʈ�� ������ �� �����ϴ�."
					"\r\n���α׷��� �����մϴ�.", "����!", MB_ICONERROR);
				EndDialog(hDlg, 0);	
			}
			else {
				while (g_bStart == 0); // ���� ���� ���� ��ٸ� (���� ���� : 1, ���� ���� : 2)
				if (g_bStart == 2) {
					g_bStart = 0;
					return FALSE;
				}
				EnableWindow(hButtonConnect, FALSE);
				EnableWindow(hButtonIsIPv6, FALSE);
				EnableWindow(hEditIPaddr, FALSE);
				EnableWindow(hEditPort, FALSE);
				EnableWindow(hButtonIsUDP, FALSE);
				EnableWindow(g_hButtonSendMsg, TRUE);
				EnableWindow(hEditUserID, FALSE);
				EnableWindow(g_chooseColorButton, TRUE);
				EnableWindow(hWidthSelect, TRUE);
				EnableWindow(hFileSelectButton, TRUE);
				EnableWindow(hFileSendButton, TRUE);
				EnableWindow(hRectButton, TRUE);
				EnableWindow(hCircleButton, TRUE);
				EnableWindow(hPencilButton, TRUE);
				EnableWindow(hEraseButton, TRUE);
				EnableWindow(hEditMsg, TRUE);
				SetFocus(hEditMsg);
			}
			return TRUE;

		// �޽��� ���� ��ư �̺�Ʈ
		case IDC_SENDMSG:
		{
			// �б� �ϷḦ ��ٸ�
			WaitForSingleObject(g_hReadEvent, INFINITE);
			type = CHATTING;
			char msg[1024] = { 0 };
			GetDlgItemText(hDlg, IDC_MSG, msg, 1024);
			g_chatmsglen = strlen(msg);
			if (g_chatmsg) free(g_chatmsg);
			g_chatmsg = (char*)malloc(g_chatmsglen+1);
			strncpy(g_chatmsg, msg, strlen(msg));
			g_chatmsg[g_chatmsglen] = '\0';
			// ���� �ϷḦ �˸�
			SetEvent(g_hWriteEvent);
			// �Էµ� �ؽ�Ʈ ��ü�� ���� ǥ��
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;
		}

		// �ݱ� �̺�Ʈ
		case IDCANCEL:
			if (MessageBox(hDlg, "������ �����Ͻðڽ��ϱ�?",
				"����", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				if (g_isUDP) {
					int len = 9;
					send(g_sock, (char*)&len, sizeof(len), 0);
					send(g_sock, "UDP_EXIT", len, 0);
				}
				closesocket(g_sock);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;
		}
		return FALSE;
	}

	return FALSE;
}

// ���� ��� ������ �Լ�
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// IPv4�� ���
	if (g_isIPv6 == false) {
		// socket()
		if (g_isUDP == TRUE) {
			g_sock = socket(AF_INET, SOCK_DGRAM, 0);
		}
		else {
			g_sock = socket(AF_INET, SOCK_STREAM, 0);
		}

		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin_family = AF_INET;
		serveraddr.sin_addr.s_addr = inet_addr(g_ipaddr);
		serveraddr.sin_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));

		if (retval == SOCKET_ERROR) err_quit("connect()");
	}
	// IPv6�� ���
	else {
		// socket()
		if (g_isUDP == TRUE) {
			g_sock = socket(AF_INET6, SOCK_DGRAM, 0);
		}
		else {
			g_sock = socket(AF_INET6, SOCK_STREAM, 0);
		}
		if (g_sock == INVALID_SOCKET) err_quit("socket()");

		// connect()
		SOCKADDR_IN6 serveraddr;
		ZeroMemory(&serveraddr, sizeof(serveraddr));
		serveraddr.sin6_family = AF_INET6;
		int addrlen = sizeof(serveraddr);
		WSAStringToAddress(g_ipaddr, AF_INET6, NULL,
			(SOCKADDR*)&serveraddr, &addrlen);
		serveraddr.sin6_port = htons(g_port);
		retval = connect(g_sock, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
		if (retval == SOCKET_ERROR) err_quit("connect()");
	}

	// �������� ���̵� �����ϰ�, ���� �ڵ带 ����
	int idLen = strlen(g_userid);
	send(g_sock, (char*)&(idLen), sizeof(idLen), 0);
	send(g_sock, g_userid, strlen(g_userid), 0);
	int status;
	recvn(g_sock, (char*)&status, sizeof(status), 0);
	// �����ڵ尡 CONNECTION_FAILED_NICKNAME_DUPLICATED�� ��� ���� �ź�(�г��� �ߺ�)
	if (status == CONNECTION_FAILED_NICKNAME_DUPLICATED) {
		MessageBox(NULL, "�ߺ��Ǵ� �г����� �����մϴ�!", "����!", MB_ICONINFORMATION);
		g_bStart = 2;
		closesocket(g_sock);
		return 0;
	}

	// ���ӿ� ������ ��� (���� �ڵ尡 CONNECTION_SUCCESS�� ���)
	MessageBox(NULL, "������ �����߽��ϴ�.", "����!", MB_ICONINFORMATION);
	// �б� & ���� ������ ����
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "�����带 ������ �� �����ϴ�."
			"\r\n���α׷��� �����մϴ�.",
			"����!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = 1; //���� ����

	// ������ ���� ���
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = 0;

	MessageBox(NULL, "������ ������ �������ϴ�", "�˸�", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// ������ �ޱ�(�б� ������, �����κ��� �����͸� ���� ���)
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	DRAWLINE_MSG* draw_msg;

	while (1) {
		int msgtype;
		int idLen;      // ID ����
		char id[128];   // ID
		char* msg;      // �޽���
		int msgLen;     // �޽��� ����

		retval = recvn(g_sock, (char*)&idLen, sizeof(idLen), 0);
		retval = recvn(g_sock, id, idLen, 0);
		id[retval] = '\0';

		// �߹���� ���
		if (!strcmp(id, "UDP_BANISH") || !strcmp(id, "TCP_BANISH")) {
			MessageBox(NULL, "�����ڷκ��� �߹���Ͽ����ϴ�.", "�˸�", MB_ICONERROR);
			closesocket(g_sock);
			TerminateThread(WriteThread, 0);
			return 0;
		}
		// Ÿ��, �޽��� ����, �޽��� ����
		retval = recvn(g_sock, (char*)&msgtype, sizeof(msgtype), 0);
		retval = recvn(g_sock, (char*)&msgLen, sizeof(msgLen), 0);
		msg = (char*)malloc(msgLen+1);
		retval = recvn(g_sock, msg, msgLen, 0);
		msg[msgLen] = '\0';

		// Ÿ���� ä���� ���
		if (msgtype == CHATTING) {
			DisplayText("[%s] %s\r\n", id, msg);
		}

		// Ÿ���� �׸����� ���
		else if (msgtype == DRAWLINE) {
			draw_msg = (DRAWLINE_MSG*)msg;
			g_drawcolor = draw_msg->color;
			g_drawwidth = draw_msg->width;
			g_drawmode = draw_msg->drawmode;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}

		//Ÿ���� �ӼӸ��� ���
		else if (msgtype == WHISPER) {
			DisplayText("[%s���� �ӼӸ�] %s\r\n", id, msg);
		}

		//Ÿ���� ����� ���
		else if (msgtype == WARNING) {
			DisplayText("[����] %s\r\n", msg);
		}

		//Ÿ���� ������ ���
		else if (msgtype == NOTICE) {
			DisplayText("[������] %s\r\n", msg);
		}
		else if (msgtype == FILESEND) {
			// ���� ����

			if (MessageBox(NULL, "������ �����Ͽ����ϴ�. ����ðڽ��ϱ�?",
				"����", MB_YESNO | MB_ICONQUESTION) == IDNO) continue;

			FILE* fp = fopen(g_receivedfilename, "wb");  // ���� �̸��� msg�� ����Ǿ� ����
			if (fp != NULL) {
				retval = fwrite(msg, sizeof(char), msgLen, fp);
				fclose(fp);
			}
			else {
				printf("���� ���� ����\n");
			}
			char cmd[50] = "start ";
			char* file_type = get_file_signature(g_receivedfilename);
			if (file_type == NULL) {
				printf("���� �ñ״�ó Ȯ�� ����\n");
			}

			else if (strcmp(file_type, "jpg") == 0 || strcmp(file_type, "png") == 0) {
				strcat(cmd, "mspaint ");
				system(strcat(cmd, g_receivedfilename));
			}
			else if (strcmp(file_type, "pdf") == 0) {
				system(strcat(cmd, g_receivedfilename));
			}
			else {
				strcat(cmd, "notepad ");
				system(strcat(cmd, g_receivedfilename));
			}
		}
		else if (msgtype == FILENAME) {
			g_receivedfilename = (char*)malloc(msgLen+1);
			strcpy(g_receivedfilename, msg);
			g_receivedfilename[retval] = '\0';
		}
		free(msg);
	}
	return 0;
}

// ������ ������ (���� ������, ������ �����͸� ������ ���)
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// ������ ������ ���
	while (1) {
		// ���� �Ϸ� ��ٸ���
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// ���ڿ� ���̰� 0�̸� ������ ����
		if (type != DRAWLINE && strlen(g_chatmsg) == 0) {
			// '�޽��� ����' ��ư Ȱ��ȭ
			EnableWindow(g_hButtonSendMsg, TRUE);
			// �б� �Ϸ� �˸���
			SetEvent(g_hReadEvent);
			continue;
		}

		// UDP�� ��� -> ID ���� �� Ÿ��, �޽��� ����, �޽��� ����
		if (g_isUDP) {
			int idLen = strlen(g_userid);
			send(g_sock, (char*)&(idLen), sizeof(idLen), 0);
			send(g_sock, g_userid, strlen(g_userid), 0);
		}
		// TCP�� ��� -> Ÿ��, �޽��� ����, �޽��� ����
		retval = send(g_sock, (char*)&type, sizeof(type), 0);
		
		if (type == DRAWLINE) {
			retval = send(g_sock, (char*)&g_drawmsgsize, sizeof(int), 0);
			retval = send(g_sock, (char *)&g_drawmsg, g_drawmsgsize, 0);
		}
		else {
			retval = send(g_sock, (char*)&g_chatmsglen, sizeof(int), 0);
			retval = send(g_sock, g_chatmsg, g_chatmsglen, 0);
		}
		
		if (retval == SOCKET_ERROR) {
			continue;
		}
		// '�޽��� ����' ��ư Ȱ��ȭ
		EnableWindow(g_hButtonSendMsg, TRUE);
		// �б� �Ϸ� �˸���
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// �ڽ� ������ ���ν���
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	
	int cx, cy;
	PAINTSTRUCT ps;
	RECT rect;
	HPEN hPen, hOldPen;
	static HBITMAP hBitmap;
	static HDC hDCMem;
	static int x0, y0;
	static int x1, y1;
	static BOOL bDrawing = FALSE;

	switch (uMsg) {
	case WM_CREATE:
		hDC = GetDC(hWnd);

		// ȭ���� ������ ��Ʈ�� ����
		cx = GetDeviceCaps(hDC, HORZRES);
		cy = GetDeviceCaps(hDC, VERTRES);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

		// �޸� DC ����
		hDCMem = CreateCompatibleDC(hDC);

		// ��Ʈ�� ���� �� �޸� DC ȭ���� ������� ĥ��
		SelectObject(hDCMem, hBitmap);
		SelectObject(hDCMem, GetStockObject(WHITE_BRUSH));
		SelectObject(hDCMem, GetStockObject(WHITE_PEN));
		Rectangle(hDCMem, 0, 0, cx, cy);

		ReleaseDC(hWnd, hDC);
		return 0;
	case WM_LBUTTONDOWN:
		x0 = LOWORD(lParam);
		y0 = HIWORD(lParam);
		bDrawing = TRUE;
		return 0;
	case WM_MOUSEMOVE:
		x1 = LOWORD(lParam);
		y1 = HIWORD(lParam);
		if (bDrawing && g_bStart == 1 && g_drawmsg.drawmode == PENCIL_MODE) {
			// �� �׸��� �޽��� ������ (��ǥ, ���� ����)
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			type = DRAWLINE;
			SetEvent(g_hWriteEvent);

			x0 = x1;
			y0 = y1;
		}
		else {
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;
		}
		return 0;
	case WM_LBUTTONUP:
	{
		
		bDrawing = FALSE;
		if (g_drawmsg.drawmode != PENCIL_MODE) {
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			type = DRAWLINE;
			SetEvent(g_hWriteEvent);
		}
		return 0;
	}
	case WM_DRAWIT:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawwidth, g_drawcolor);

		// ȭ�鿡 �׸���
		hOldPen = (HPEN)SelectObject(hDC, hPen);
		if (g_drawmode == PENCIL_MODE) {
			MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
		}
		else if (g_drawmode == CIRCLE_MODE) {
			int a = LOWORD(wParam) - LOWORD(lParam); if (a < 0) a *= -1;
			int b = HIWORD(wParam) - HIWORD(lParam); if (b < 0) b *= -1;
			int centerX = (LOWORD(wParam) + LOWORD(lParam)) / 2;
			int centerY = (HIWORD(wParam) + HIWORD(lParam)) / 2;
			int tempX = centerX - a;
			int tempY = centerY;
			MoveToEx(hDC, tempX, tempY, NULL);
			for (int x = tempX+1; x<=centerX+a; x++)
			{
				tempX = x;
				tempY = sqrt(b * b - (b * b) * (x-centerX) * (x-centerX) / (a * a)) + centerY;
				LineTo(hDC, tempX, tempY);
			}
			tempX = centerX - a;
			tempY = centerY;
			MoveToEx(hDC, tempX, tempY, NULL);
			for (int x = tempX+1; x <= centerX + a; x++)
			{
				tempX = x;
				tempY = -sqrt(b * b - (b * b) * (x - centerX) * (x - centerX) / (a * a)) + centerY;
				// ���� ���� ������Ʈ �������� ���� �̾��ִ� ���� �׸���.
				LineTo(hDC, tempX, tempY);
			}
		}
		else if (g_drawmode == RECT_MODE) {
			MoveToEx(hDC, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDC, LOWORD(lParam), HIWORD(wParam));
			LineTo(hDC, LOWORD(lParam), HIWORD(lParam));
			LineTo(hDC, LOWORD(wParam), HIWORD(lParam));
			LineTo(hDC, LOWORD(wParam), HIWORD(wParam));
		}
		hOldPen = (HPEN)SelectObject(hDCMem, hPen);

		if (g_drawmode == PENCIL_MODE) {
			MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
		}
		else if (g_drawmode == CIRCLE_MODE) {
			int a = LOWORD(wParam) - LOWORD(lParam); if (a < 0) a *= -1;
			int b = HIWORD(wParam) - HIWORD(lParam); if (b < 0) b *= -1;
			int centerX = (LOWORD(wParam) + LOWORD(lParam)) / 2;
			int centerY = (HIWORD(wParam) + HIWORD(lParam)) / 2;
			int tempX = centerX - a;
			int tempY = centerY;
			MoveToEx(hDCMem, tempX, tempY, NULL);
			for (int x = tempX + 1; x <= centerX + a; x++)
			{
				tempX = x;
				tempY = sqrt(b * b - (b * b) * (x - centerX) * (x - centerX) / (a * a)) + centerY;
				LineTo(hDCMem, tempX, tempY);
			}
			tempX = centerX - a;
			tempY = centerY;
			MoveToEx(hDCMem, tempX, tempY, NULL);
			for (int x = tempX + 1; x <= centerX + a; x++)
			{
				tempX = x;
				tempY = -sqrt(b * b - (b * b) * (x - centerX) * (x - centerX) / (a * a)) + centerY;
				// ���� ���� ������Ʈ �������� ���� �̾��ִ� ���� �׸���.
				LineTo(hDCMem, tempX, tempY);
			}
		}
		else if (g_drawmode == RECT_MODE) {
			MoveToEx(hDCMem, LOWORD(wParam), HIWORD(wParam), NULL);
			LineTo(hDCMem, LOWORD(lParam), HIWORD(wParam));
			LineTo(hDCMem, LOWORD(lParam), HIWORD(lParam));
			LineTo(hDCMem, LOWORD(wParam), HIWORD(lParam));
			LineTo(hDCMem, LOWORD(wParam), HIWORD(wParam));
		}
		
		SelectObject(hDC, hOldPen);

		DeleteObject(hPen);
		ReleaseDC(hWnd, hDC);
		return 0;
	case WM_PAINT:
		hDC = BeginPaint(hWnd, &ps);

		// �޸� ��Ʈ�ʿ� ����� �׸��� ȭ�鿡 ����
		GetClientRect(hWnd, &rect);
		BitBlt(hDC, 0, 0, rect.right - rect.left,
			rect.bottom - rect.top, hDCMem, 0, 0, SRCCOPY);

		EndPaint(hWnd, &ps);
		return 0;
	case WM_DESTROY:
		DeleteObject(hBitmap);
		DeleteDC(hDCMem);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ����Ʈ ��Ʈ�ѿ� ���ڿ� ���
void DisplayText(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(g_hEditStatus);
	SendMessage(g_hEditStatus, EM_SETSEL, nLength, nLength);
	SendMessage(g_hEditStatus, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}

// ����� ���� ������ ���� �Լ�
int recvn(SOCKET s, char* buf, int len, int flags)
{
	int received;
	char* ptr = buf;
	int left = len;

	while (left > 0) {
		received = recv(s, ptr, left, flags);
		if (received == SOCKET_ERROR)
			return SOCKET_ERROR;
		else if (received == 0)
			break;
		left -= received;
		ptr += received;
	}

	return (len - left);
}

// ���� �Լ� ���� ��� �� ����
void err_quit(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

// ���� �Լ� ���� ���
void err_display(char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	printf("[%s] %s", msg, (char*)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

char* get_file_signature(char* filename) {
	FILE* file = fopen(filename, "rb");
	if (file == NULL) {
		return NULL;
	}

	unsigned char buffer[4];
	if (fread(buffer, sizeof(unsigned char), 4, file) != 4) {
		fclose(file);
		return NULL;
	}
	fclose(file);

	// ���� �ñ״�ó Ȯ��
	if (buffer[0] == 0xFF && buffer[1] == 0xD8) {
		return "jpg";
	}
	else if (buffer[0] == 0x89 && buffer[1] == 0x50 && buffer[2] == 0x4E && buffer[3] == 0x47) {
		return "png";
	}
	else if (buffer[0] == 0x25 && buffer[1] == 0x50 && buffer[2] == 0x44 && buffer[3] == 0x46) {
		return "pdf";
	}
	else {
		return NULL;
	}
}