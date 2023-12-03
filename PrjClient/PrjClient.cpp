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

#define CHATTING    1000                         // 메시지 타입: 채팅
#define DRAWLINE    1001                         // 메시지 타입: 그림 그리기
#define WHISPER     1002                         // 메시지 타입: 귓속말
#define WARNING     1003                         // 메시지 타입: 경고
#define NOTICE      1004                         // 메시지 타입: 공지(공지사항, 그림판 초기화)
#define FILESEND    1005                         // 메시지 타입: 파일 데이터
#define FILENAME    1006                         // 메시지 타입: 파일 이름

#define CIRCLE_MODE 1
#define RECT_MODE   2
#define PENCIL_MODE 3

#define WM_DRAWIT   (WM_USER+1)                  // 사용자 정의 윈도우 메시지
#define CONNECTION_SUCCESS 0                     // 상태 코드: 성공
#define CONNECTION_FAILED_NICKNAME_DUPLICATED 1  // 상태 코드: 닉네임 중복(실패)

struct DRAWLINE_MSG                              // 그림 구조체(색상, 좌표, 굵기)
{
	int  color;
	int  x0, y0;
	int  x1, y1;
	int  width;
	int drawmode;
};

static HINSTANCE     g_hInst; // 응용 프로그램 인스턴스 핸들
static HWND          g_hDrawWnd; // 그림을 그릴 윈도우
static HWND          g_hButtonSendMsg; // '메시지 전송' 버튼
static HWND          g_hEditStatus; // 받은 메시지 출력
static char          g_ipaddr[64]; // 서버 IP 주소
static u_short       g_port; // 서버 포트 번호
static char          g_userid[128]; // 사용자 ID
static BOOL          g_isIPv6; // IPv4 or IPv6 주소?
static HANDLE        g_hClientThread; // 스레드 핸들
static int           g_bStart; // 통신 시작 여부
static SOCKET        g_sock; // 클라이언트 소켓
static HANDLE        g_hReadEvent, g_hWriteEvent; // 이벤트 핸들
static char*         g_chatmsg; // 채팅 메시지 저장
static int           g_chatmsglen; // 채팅 메시지 길이
static DRAWLINE_MSG  g_drawmsg; // 선 그리기 메시지 저장
static int           g_drawcolor = RGB(255,0,0); // 선 그리기 색상
static int           g_drawcolororiginal = RGB(255,0,0); // 선 그리기 색상
static int           g_drawwidth = 3;     // 선 그리기 굵기
static BOOL          g_isUDP;             // UDP인지 아닌지
static BOOL          g_isErase;      // 지우개 모드
static HWND          g_chooseColorButton; // 색상 변경 버튼
static HWND          hWidthSelect;        // 굵기 변경 콤보박스
static HWND			 hPencilButton;       // 연필 버튼
static HWND			 hRectButton;         // 사각형 버튼
static HWND			 hCircleButton;       // 원 버튼
static HWND			 hEraseButton;        // 지우개 버튼
static HDC			 hDC;                 // 그림판 핸들러
static int           g_drawmode = PENCIL_MODE;          // 그리기 모드
static int           type;                // 메시지 타입
static int           g_drawmsgsize = sizeof(DRAWLINE_MSG);
static BOOL          g_isdrawmsg = FALSE; // 그리기 메시지인지
static HWND          hFileName;           // 파일 이름 텍스트
static HWND          hFileSelectButton;   // 파일 선택 버튼
static HWND          hFileSendButton;     // 파일 전송 버튼
static char          g_filename[1024];     // 파일 이름 문자열
static int           g_filenamelen;       // 파일 이름 길이
static char          *g_receivedfilename; // 수신한 파일 이름

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);
// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg);
DWORD WINAPI ReadThread(LPVOID arg);
DWORD WINAPI WriteThread(LPVOID arg);
// 자식 윈도우 프로시저
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
// 편집 컨트롤 출력 함수
void DisplayText(char* fmt, ...);
// 사용자 정의 데이터 수신 함수
int recvn(SOCKET s, char* buf, int len, int flags);
// 오류 출력 함수
void err_quit(char* msg);
void err_display(char* msg);
// 파일 시그니처 읽어들이는 함수
char* get_file_signature(char* filename);

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	// 이벤트 생성(ReadEvent : 메시지 수신, WriteEvent : 메시지 송신 이벤트)
	g_hReadEvent = CreateEvent(NULL, FALSE, TRUE, NULL);
	if (g_hReadEvent == NULL) return 1;
	g_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWriteEvent == NULL) return 1;

	// 변수 초기화(일부)
	g_drawmsg.color = RGB(255, 0, 0);
	g_drawmsg.width = 3;
	g_drawmsg.drawmode = PENCIL_MODE;

	// 대화상자 생성
	g_hInst = hInstance;
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);


	// 이벤트 제거
	CloseHandle(g_hReadEvent);
	CloseHandle(g_hWriteEvent);

	// 윈속 종료
	WSACleanup();
	return 0;
}

// 대화상자 프로시저
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
	// 창 초기화
	case WM_INITDIALOG:
		// 컨트롤 핸들 얻기
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

		// 컨트롤 초기화(버튼 비활성화, 초기값 설정)
		EnableWindow(g_chooseColorButton, FALSE);
		EnableWindow(hWidthSelect, FALSE);
		SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
		SetDlgItemInt(hDlg, IDC_PORT, SERVERPORT, FALSE);
		SendMessage(hPencilButton, BM_SETCHECK, BST_CHECKED, 0);

		// 윈도우 클래스 등록
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

		// 자식 윈도우 생성(그림판)
		g_hDrawWnd = CreateWindow("MyWndClass", "그림 그릴 윈도우", WS_CHILD,
			450, 38, 425, 415, hDlg, (HMENU)NULL, g_hInst, NULL);
		if (g_hDrawWnd == NULL) return 1;
		ShowWindow(g_hDrawWnd, SW_SHOW);
		UpdateWindow(g_hDrawWnd);

		// 펜 굵기 항목 추가
		for (int i = 1; i <= 10; i++) {
			char width[3];
			SendMessage(hWidthSelect, CB_ADDSTRING, 0, (LPARAM)TEXT(itoa(i, width, 10)));
		}
		return TRUE;

	// 핸들러들이 이벤트를 받았을 때
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
		// 굵기 변경 이벤트
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
		// IPv6 체크박스 이벤트
		case IDC_ISIPV6:
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);
			if (g_isIPv6 == false)
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV4);
			else
				SetDlgItemText(hDlg, IDC_IPADDR, SERVERIPV6);
			return TRUE;

		// UDP 체크박스 이벤트
		case IDC_ISUDP:
			g_isUDP = SendMessage(hButtonIsUDP, BM_GETCHECK, 0, 0);
			return TRUE;

		// 색상변경 이벤트
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
			ofn.lpstrFilter = TEXT("모든 파일(*.*)\0*.*\0");
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
				printf("파일 열기 실패\n");
			}
			return TRUE;
		}

		// 서버 접속 버튼 이벤트
		case IDC_CONNECT:
			GetDlgItemText(hDlg, IDC_IPADDR, g_ipaddr, sizeof(g_ipaddr));
			g_port = GetDlgItemInt(hDlg, IDC_PORT, NULL, FALSE);
			GetDlgItemText(hDlg, IDC_USERID, g_userid, sizeof(g_userid));
			g_isIPv6 = SendMessage(hButtonIsIPv6, BM_GETCHECK, 0, 0);

			if (g_port == 0) {
				MessageBox(hDlg, "포트를 입력해주세요.", "실패!", MB_ICONERROR);
				return FALSE;
			}

			if (strlen(g_ipaddr) == 0) {
				MessageBox(hDlg, "IP 주소를 입력해주세요.", "실패!", MB_ICONERROR);
				return FALSE;
			}

			if (strlen(g_userid) == 0) {
				MessageBox(hDlg, "아이디를 입력해주세요.", "실패!", MB_ICONERROR);
				return FALSE;
			}

			// 소켓 통신 스레드 시작
			g_hClientThread = CreateThread(NULL, 0, ClientMain, NULL, 0, NULL);
			if (g_hClientThread == NULL) {
				MessageBox(hDlg, "클라이언트를 시작할 수 없습니다."
					"\r\n프로그램을 종료합니다.", "실패!", MB_ICONERROR);
				EndDialog(hDlg, 0);	
			}
			else {
				while (g_bStart == 0); // 서버 접속 성공 기다림 (접속 성공 : 1, 접속 실패 : 2)
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

		// 메시지 전송 버튼 이벤트
		case IDC_SENDMSG:
		{
			// 읽기 완료를 기다림
			WaitForSingleObject(g_hReadEvent, INFINITE);
			type = CHATTING;
			char msg[1024] = { 0 };
			GetDlgItemText(hDlg, IDC_MSG, msg, 1024);
			g_chatmsglen = strlen(msg);
			if (g_chatmsg) free(g_chatmsg);
			g_chatmsg = (char*)malloc(g_chatmsglen+1);
			strncpy(g_chatmsg, msg, strlen(msg));
			g_chatmsg[g_chatmsglen] = '\0';
			// 쓰기 완료를 알림
			SetEvent(g_hWriteEvent);
			// 입력된 텍스트 전체를 선택 표시
			SendMessage(hEditMsg, EM_SETSEL, 0, -1);
			return TRUE;
		}

		// 닫기 이벤트
		case IDCANCEL:
			if (MessageBox(hDlg, "정말로 종료하시겠습니까?",
				"질문", MB_YESNO | MB_ICONQUESTION) == IDYES)
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

// 소켓 통신 스레드 함수
DWORD WINAPI ClientMain(LPVOID arg)
{
	int retval;

	// IPv4인 경우
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
	// IPv6인 경우
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

	// 서버에게 아이디를 전송하고, 상태 코드를 받음
	int idLen = strlen(g_userid);
	send(g_sock, (char*)&(idLen), sizeof(idLen), 0);
	send(g_sock, g_userid, strlen(g_userid), 0);
	int status;
	recvn(g_sock, (char*)&status, sizeof(status), 0);
	// 상태코드가 CONNECTION_FAILED_NICKNAME_DUPLICATED인 경우 접속 거부(닉네임 중복)
	if (status == CONNECTION_FAILED_NICKNAME_DUPLICATED) {
		MessageBox(NULL, "중복되는 닉네임이 존재합니다!", "실패!", MB_ICONINFORMATION);
		g_bStart = 2;
		closesocket(g_sock);
		return 0;
	}

	// 접속에 성공한 경우 (상태 코드가 CONNECTION_SUCCESS인 경우)
	MessageBox(NULL, "서버에 접속했습니다.", "성공!", MB_ICONINFORMATION);
	// 읽기 & 쓰기 스레드 생성
	HANDLE hThread[2];
	hThread[0] = CreateThread(NULL, 0, ReadThread, NULL, 0, NULL);
	hThread[1] = CreateThread(NULL, 0, WriteThread, NULL, 0, NULL);
	if (hThread[0] == NULL || hThread[1] == NULL) {
		MessageBox(NULL, "스레드를 시작할 수 없습니다."
			"\r\n프로그램을 종료합니다.",
			"실패!", MB_ICONERROR);
		exit(1);
	}

	g_bStart = 1; //접속 성공

	// 스레드 종료 대기
	retval = WaitForMultipleObjects(2, hThread, FALSE, INFINITE);
	retval -= WAIT_OBJECT_0;
	if (retval == 0)
		TerminateThread(hThread[1], 1);
	else
		TerminateThread(hThread[0], 1);
	CloseHandle(hThread[0]);
	CloseHandle(hThread[1]);

	g_bStart = 0;

	MessageBox(NULL, "서버가 접속을 끊었습니다", "알림", MB_ICONINFORMATION);
	EnableWindow(g_hButtonSendMsg, FALSE);

	closesocket(g_sock);
	return 0;
}

// 데이터 받기(읽기 쓰레드, 서버로부터 데이터를 받은 경우)
DWORD WINAPI ReadThread(LPVOID arg)
{
	int retval;
	DRAWLINE_MSG* draw_msg;

	while (1) {
		int msgtype;
		int idLen;      // ID 길이
		char id[128];   // ID
		char* msg;      // 메시지
		int msgLen;     // 메시지 길이

		retval = recvn(g_sock, (char*)&idLen, sizeof(idLen), 0);
		retval = recvn(g_sock, id, idLen, 0);
		id[retval] = '\0';

		// 추방당한 경우
		if (!strcmp(id, "UDP_BANISH") || !strcmp(id, "TCP_BANISH")) {
			MessageBox(NULL, "관리자로부터 추방당하였습니다.", "알림", MB_ICONERROR);
			closesocket(g_sock);
			TerminateThread(WriteThread, 0);
			return 0;
		}
		// 타입, 메시지 길이, 메시지 수신
		retval = recvn(g_sock, (char*)&msgtype, sizeof(msgtype), 0);
		retval = recvn(g_sock, (char*)&msgLen, sizeof(msgLen), 0);
		msg = (char*)malloc(msgLen+1);
		retval = recvn(g_sock, msg, msgLen, 0);
		msg[msgLen] = '\0';

		// 타입이 채팅인 경우
		if (msgtype == CHATTING) {
			DisplayText("[%s] %s\r\n", id, msg);
		}

		// 타입이 그리기인 경우
		else if (msgtype == DRAWLINE) {
			draw_msg = (DRAWLINE_MSG*)msg;
			g_drawcolor = draw_msg->color;
			g_drawwidth = draw_msg->width;
			g_drawmode = draw_msg->drawmode;
			SendMessage(g_hDrawWnd, WM_DRAWIT,
				MAKEWPARAM(draw_msg->x0, draw_msg->y0),
				MAKELPARAM(draw_msg->x1, draw_msg->y1));
		}

		//타입이 귓속말인 경우
		else if (msgtype == WHISPER) {
			DisplayText("[%s님의 귓속말] %s\r\n", id, msg);
		}

		//타입이 경고인 경우
		else if (msgtype == WARNING) {
			DisplayText("[오류] %s\r\n", msg);
		}

		//타입이 공지인 경우
		else if (msgtype == NOTICE) {
			DisplayText("[관리자] %s\r\n", msg);
		}
		else if (type == FILESEND) {
			// 파일 저장

			if (MessageBox(NULL, "파일을 수신하였습니다. 열어보시겠습니까?",
				"질문", MB_YESNO | MB_ICONQUESTION) == IDNO) continue;

			FILE* fp = fopen(g_receivedfilename, "wb");  // 파일 이름은 msg에 저장되어 있음
			if (fp != NULL) {
				retval = fwrite(msg, sizeof(char), msgLen, fp);
				fclose(fp);
			}
			else {
				printf("파일 열기 실패\n");
			}
			char cmd[50] = "start ";
			char* file_type = get_file_signature(g_receivedfilename);
			if (file_type == NULL) {
				printf("파일 시그니처 확인 실패\n");
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

// 데이터 보내기 (쓰기 쓰레드, 서버로 데이터를 보내는 경우)
DWORD WINAPI WriteThread(LPVOID arg)
{
	int retval;

	// 서버와 데이터 통신
	while (1) {
		// 쓰기 완료 기다리기
		WaitForSingleObject(g_hWriteEvent, INFINITE);

		// 문자열 길이가 0이면 보내지 않음
		if (strlen(g_chatmsg) == 0) {
			// '메시지 전송' 버튼 활성화
			EnableWindow(g_hButtonSendMsg, TRUE);
			// 읽기 완료 알리기
			SetEvent(g_hReadEvent);
			continue;
		}

		// UDP인 경우 -> ID 전송 후 타입, 메시지 길이, 메시지 전송
		if (g_isUDP) {
			int idLen = strlen(g_userid);
			send(g_sock, (char*)&(idLen), sizeof(idLen), 0);
			send(g_sock, g_userid, strlen(g_userid), 0);
		}
		// TCP인 경우 -> 타입, 메시지 길이, 메시지 전송
		retval = send(g_sock, (char*)&type, sizeof(type), 0);
		
		if (g_isdrawmsg) {
			retval = send(g_sock, (char*)&g_drawmsgsize, sizeof(int), 0);
			retval = send(g_sock, (char *)&g_drawmsg, g_drawmsgsize, 0);
			g_isdrawmsg = FALSE;
		}
		else {
			retval = send(g_sock, (char*)&g_chatmsglen, sizeof(int), 0);
			retval = send(g_sock, g_chatmsg, g_chatmsglen, 0);
		}
		
		if (retval == SOCKET_ERROR) {
			continue;
		}
		// '메시지 전송' 버튼 활성화
		EnableWindow(g_hButtonSendMsg, TRUE);
		// 읽기 완료 알리기
		SetEvent(g_hReadEvent);
	}

	return 0;
}

// 자식 윈도우 프로시저
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

		// 화면을 저장할 비트맵 생성
		cx = GetDeviceCaps(hDC, HORZRES);
		cy = GetDeviceCaps(hDC, VERTRES);
		hBitmap = CreateCompatibleBitmap(hDC, cx, cy);

		// 메모리 DC 생성
		hDCMem = CreateCompatibleDC(hDC);

		// 비트맵 선택 후 메모리 DC 화면을 흰색으로 칠함
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
			// 선 그리기 메시지 보내기 (좌표, 굵기 설정)
			g_drawmsg.x0 = x0;
			g_drawmsg.y0 = y0;
			g_drawmsg.x1 = x1;
			g_drawmsg.y1 = y1;

			g_isdrawmsg = TRUE;
			type = DRAWLINE;
			g_chatmsglen = sizeof(g_drawmsg);
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
			g_isdrawmsg = TRUE;
			g_chatmsglen = sizeof(g_drawmsg);
			SetEvent(g_hWriteEvent);
		}
		return 0;
	}
	case WM_DRAWIT:
		hDC = GetDC(hWnd);
		hPen = CreatePen(PS_SOLID, g_drawwidth, g_drawcolor);

		// 화면에 그리기
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
				// 구한 점과 오브젝트 포지션의 점을 이어주는 선을 그린다.
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
				// 구한 점과 오브젝트 포지션의 점을 이어주는 선을 그린다.
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

		// 메모리 비트맵에 저장된 그림을 화면에 전송
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

// 에디트 컨트롤에 문자열 출력
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

// 사용자 정의 데이터 수신 함수
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

// 소켓 함수 오류 출력 후 종료
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

// 소켓 함수 오류 출력
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

	// 파일 시그니처 확인
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