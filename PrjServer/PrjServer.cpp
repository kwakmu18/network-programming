#pragma comment(lib, "ws2_32")
#include "resource.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#define IDSIZE                                128    // ID 최대 길이
#define CONNECTION_SUCCESS                    0      // 상태 코드 (성공)
#define CONNECTION_FAILED_NICKNAME_DUPLICATED 1      // 상태 코드 (실패, ID 중복)
#define CHATTING                              1000   // 메시지 타입 (채팅)
#define DRAWLINE                              1001   // 메시지 타입 (그리기)
#define WHISPER                               1002   // 메시지 타입 (귓속말)
#define WARNING                               1003   // 메시지 타입 (경고)
#define NOTICE                                1004   // 메시지 타입 (공지)

struct TCPSOCKETINFO        // TCP 소켓 구조체(소켓, IPv6 여부, 버퍼, 버퍼크기, 타입, ID)
{
	SOCKET sock;
	bool   isIPv6;
	char*  buf;
	int    bufSize;
	int    type;
	char   userID[IDSIZE];
};

struct UDPSOCKETINFO {      // UDP 소켓 구조체(IPv6 여부, ID, IPv4 주소, IPv6 주소)
	bool isIPv6;
	char userID[IDSIZE];
	SOCKADDR_IN addrv4;
	SOCKADDR_IN6 addrv6;
};

struct DRAWLINE_MSG         // 그리기 구조체
{
	int  color;
	int  x0, y0;
	int  x1, y1;
	int width;
};
//대화상자 프로시저
BOOL CALLBACK DlgProc(HWND, UINT, WPARAM, LPARAM);

// TCP, UDP 소켓 구조체 관리 함수
BOOL AddTCPSocketInfo(SOCKET sock, bool isIPv6, char* userID);
BOOL AddUDPv4SocketInfo(SOCKADDR_IN addr, char* userID);
BOOL AddUDPv6SocketInfo(SOCKADDR_IN6 addr, char* userID);
void RemoveTCPSocketInfo(int nIndex, int mode);
void RemoveUDPSocketInfo(int nIndex, int mode);

// IPv4, IPv6 주소 비교 함수
BOOL compareAddressv4(SOCKADDR_IN addr1, SOCKADDR_IN addr2);
BOOL compareAddressv6(SOCKADDR_IN6 addr1, SOCKADDR_IN6 addr2);

// 메시지/귓속말 전달
void sendWhisper(char* userID, char* toID, char* data, int dataLen, int type);
void sendData(char* userID, char* data, int dataLen, int type);

// 각종 오류 처리 함수
void err_quit(char* msg);
void err_display(char* msg);
void DisplayText(char* fmt, ...);

int recvn(SOCKET s, char* buf, int len, int flags);

// 통신 쓰레드
DWORD WINAPI TCPMain(LPVOID arg);
DWORD WINAPI UDPv4Main(LPVOID arg);
DWORD WINAPI UDPv6Main(LPVOID arg);

static HANDLE        serverTCPThread, serverUDPv4Thread, serverUDPv6Thread; // 쓰레드 핸들러
static HINSTANCE     g_hInst;												// 응용 프로그램 인스턴스 핸들
static HWND          hBanishButton;											// 추방 버튼 핸들러
static HWND			 hUserIDText;											// ID,IP,프로토콜,포트 텍스트 핸들러
static HWND			 hUserIPText;
static HWND			 hUserProtoText;
static HWND			 hUserPortText;
static HWND			 hUserList;												// 사용자 목록 핸들러
static HWND			 hUserLog;												// 로그 핸들러
static BOOL          isBanishing = FALSE;									// 현재 추방 중인지 확인
static BOOL          isServerStarted = FALSE;								// 서버가 시작했는지
static HWND          hImageResetButton;										// 그림판 초기화 버튼 핸들러
static HWND          hNoticeButton;											// 공지사항 버튼 핸들러
static HWND          hNotice;												// 공지사항 입력 핸들러
static HWND          hServerStartButton;									// 서버 시작 버튼 핸들러
static HWND          hServerPort;											// 서버 포트 입력 핸들러
static char          whisperErrorMessage[50] = "올바르지 않은 형식입니다.";     // 공지용 문장
static char          whisperNoIDMessage[50] = "존재하지 않는 ID입니다.";
static char          resetImageMessage[50] = "그림판을 초기화했습니다.";
static int           serverPort = 9000;                                     // 서버 포트(기본값 = 9000)

// 소켓 정보 저장을 위한 구조체와 변수
int nTotalTCPSockets = 0, nTotalUDPSockets = 0; // 총 TCP 접속자 수, UDP 접속자 수
TCPSOCKETINFO* TCPSocketInfoArray[FD_SETSIZE];  // TCP 접속자 관리 배열
UDPSOCKETINFO* UDPSocketInfoArray[FD_SETSIZE];  // UDP 접속자 관리 배열

// TCP, UDP 소켓
SOCKET listen_sockv4, listen_sockv6, udp_sockv4, udp_sockv6;

// 메인 함수
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	g_hInst = hInstance;
	
	serverTCPThread = CreateThread(NULL, 0, TCPMain, NULL, 0, NULL);
	serverUDPv4Thread = CreateThread(NULL, 0, UDPv4Main, NULL, 0, NULL);
	serverUDPv6Thread = CreateThread(NULL, 0, UDPv6Main, NULL, 0, NULL);

	if (serverTCPThread == NULL || serverUDPv4Thread == NULL || serverUDPv6Thread == NULL) {
		MessageBox(NULL, "서버 실행에 오류가 발생했습니다.", "오류", MB_ICONERROR);
		return 1;
	}
	HANDLE handles[3] = { serverTCPThread, serverUDPv4Thread, serverUDPv6Thread };
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, DlgProc);
	WaitForMultipleObjects(3, handles, TRUE, INFINITE);
	return 0;
}

// 대화상자 프로시저
BOOL CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	switch (uMsg) {
	case WM_INITDIALOG:
		// 컨트롤 핸들 얻기
		hBanishButton = GetDlgItem(hDlg, IDC_BANISHBUTTON);
		hUserIDText = GetDlgItem(hDlg, IDC_USERID);
		hUserIPText = GetDlgItem(hDlg, IDC_USERIP);
		hUserProtoText = GetDlgItem(hDlg, IDC_USERPROTO);
		hUserPortText = GetDlgItem(hDlg, IDC_USERPORT);
		hUserList = GetDlgItem(hDlg, IDC_USERLIST);
		hUserLog = GetDlgItem(hDlg, IDC_USERLOG);
		hImageResetButton = GetDlgItem(hDlg, IDC_IMAGERESETBUTTON);
		hNoticeButton = GetDlgItem(hDlg, IDC_SENDNOTICEBUTTON);
		hNotice = GetDlgItem(hDlg, IDC_NOTICE);
		hServerStartButton = GetDlgItem(hDlg, IDC_SERVERSTART);
		hServerPort = GetDlgItem(hDlg, IDC_SERVERPORT);
		SetWindowText(hServerPort, "9000"); // 포트 기본값 설정(9000)
		break;

	// 핸들러들이 이벤트를 받았을 때
	case WM_COMMAND:
		switch (LOWORD(wParam)) {

		// 서버 시작 버튼인 경우
		case IDC_SERVERSTART:
		{
			char msg[6] = { 0 };
			GetDlgItemText(hDlg, IDC_SERVERPORT, msg, 6);
			if (strlen(msg) == 0) {
				MessageBox(hDlg, "포트 번호를 입력해주세요.", "오류", MB_ICONERROR);
				return FALSE;
			}
			for (int i = 0; i < strlen(msg); i++) {
				if (msg[i] < '0' || msg[i]>'9') {
					MessageBox(hDlg, "포트 번호는 숫자만 입력할 수 있습니다.", "오류", MB_ICONERROR);
					return FALSE;
				}
			}

			serverPort = atoi(msg);
			isServerStarted = TRUE; // 서버 시작
			MessageBox(NULL, "서버를 시작하였습니다.", "알림", MB_ICONINFORMATION);
			EnableWindow(hServerPort, FALSE);
			EnableWindow(hServerStartButton, FALSE);
			return TRUE;
		}

		// 창 닫기 눌렀을 때
		case IDCANCEL:
			if (MessageBox(hDlg, "정말로 종료하시겠습니까?",
				"질문", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				TerminateThread(serverTCPThread, 0);
				TerminateThread(serverUDPv4Thread, 0);
				TerminateThread(serverUDPv6Thread, 0);
				EndDialog(hDlg, IDCANCEL);
			}
			return TRUE;

		// 추방 버튼 눌렀을 때
		case IDC_BANISHBUTTON:
		{
			int currentIndex = SendMessage(hUserList, LB_GETCURSEL, 0, 0);
			if (currentIndex != LB_ERR) {
				SetWindowText(hUserIDText, "");
				SetWindowText(hUserIPText, "");
				SetWindowText(hUserPortText, "");
				SetWindowText(hUserProtoText, "");

				char userID[128];

				// TCP, UDP 소켓 중에서 ID가 일치하는 사용자를 골라 추방(TCP_BANISH / UDP_BANISH 전송)
				SendMessage(hUserList, LB_GETTEXT, (WPARAM)currentIndex, (LPARAM)userID);
				for (int i = 0; i < nTotalTCPSockets; i++) {
					TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
					if (!strcmp(userID, ptr->userID)) {
						int len = 11;
						send(ptr->sock, (char*)&len, sizeof(len), 0);
						send(ptr->sock, "TCP_BANISH", len, 0);
						RemoveTCPSocketInfo(i, 1);
						break;
					}
				}
				for (int i = 0; i < nTotalUDPSockets; i++) {
					UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
					if (!strcmp(userID, ptr->userID)) {
						if (ptr->isIPv6 == TRUE) {
							int len = 11;
							sendto(udp_sockv6, (char*)&len, sizeof(len), 0, (SOCKADDR*)&ptr->addrv6, sizeof(ptr->addrv6));
							sendto(udp_sockv6, "UDP_BANISH", len, 0, (SOCKADDR*)&ptr->addrv6, sizeof(ptr->addrv6));
						}
						else {
							int len = 11;
							sendto(udp_sockv4, (char*)&len, sizeof(len), 0, (SOCKADDR*)&ptr->addrv4, sizeof(ptr->addrv4));
							sendto(udp_sockv4, "UDP_BANISH", len, 0, (SOCKADDR*)&ptr->addrv4, sizeof(ptr->addrv4));
						}
						RemoveUDPSocketInfo(i, 1);
						break;
					}
				}
				char msg[150] = { 0 };
				strcat(msg, userID);
				strcat(msg, "님을 추방하였습니다.");
				sendData("관리자", msg, strlen(msg), NOTICE);
			}
			return TRUE;
		}

		// 그림 초기화 버튼 눌렀을 때
		case IDC_IMAGERESETBUTTON:
		{
			DRAWLINE_MSG msg;
			msg.color = RGB(255, 255, 255);
			char name[IDSIZE] = "관리자";
			int nameLen = strlen(name);
			for (int x = 0; x < 425; x++) {
				msg.x0 = x; msg.x1 = x;
				msg.y0 = 0; msg.y1 = 414;
				msg.width = 3;
				sendData(name, (char*)&msg, sizeof(msg), DRAWLINE);
				if (x % 80 == 0) Sleep(1);
			}
			sendData("관리자", resetImageMessage, strlen(resetImageMessage), NOTICE);
			return TRUE;
		}

		// 공지사항 전송 버튼 눌렀을 때
		case IDC_SENDNOTICEBUTTON:
			char msg[1024] = { 0 };
			GetDlgItemText(hDlg, IDC_NOTICE, msg, 1024);
			if (strlen(msg) == 0) {
				MessageBox(hDlg, "공지사항을 입력해주세요.", "오류", MB_ICONERROR);
				return FALSE;
			}
			sendData("관리자", msg, strlen(msg), NOTICE);
			return TRUE;
		}

		// 사용자 목록에서 사용자를 선택했을 경우
		if (HIWORD(wParam) == LBN_SELCHANGE && LOWORD(wParam) == IDC_USERLIST) {
			int index = SendMessage((HWND)lParam, LB_GETCURSEL, 0, 0);
			if (index != LB_ERR) {
				TCHAR itemText[128];
				SendMessage(hUserList, LB_GETTEXT, (WPARAM)index, (LPARAM)itemText);
				// TCP, UDP 소켓 중에서 ID가 일치하는 사용자를 골라 정보 표시(ID, IP, 포트번호, 프로토콜)
				for (int i = 0; i < nTotalTCPSockets; i++) {
					TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
					if (strcmp(ptr->userID, itemText)) continue;
					SetWindowText(hUserIDText, ptr->userID);
					if (ptr->isIPv6 == TRUE) {
						SOCKADDR_IN6 peeraddr;
						int peeraddrlen = sizeof(peeraddr);
						char ip[50];
						char port[6];
						getpeername(ptr->sock, (SOCKADDR*)&peeraddr, &peeraddrlen);
						SetWindowText(hUserIPText, inet_ntop(AF_INET6, &(peeraddr.sin6_addr), ip, 50));
						SetWindowText(hUserPortText, itoa(ntohs(peeraddr.sin6_port), port, 10));
						SetWindowText(hUserProtoText, "TCP/IPv6");
					}
					else {
						SOCKADDR_IN peeraddr;
						int peeraddrlen = sizeof(peeraddr);
						char port[6];
						getpeername(ptr->sock, (SOCKADDR*)&peeraddr, &peeraddrlen);
						SetWindowText(hUserIPText, inet_ntoa(peeraddr.sin_addr));
						SetWindowText(hUserPortText, itoa(ntohs(peeraddr.sin_port), port, 10));
						SetWindowText(hUserProtoText, "TCP/IPv4");
					}
					break;
				}
				for (int i = 0; i < nTotalUDPSockets; i++) {
					UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
					if (strcmp(ptr->userID, itemText)) continue;
					SetWindowText(hUserIDText, ptr->userID);
					char port[6];
					char ip[50];
					SetWindowText(hUserPortText, itoa(ntohs(ptr->addrv4.sin_port), port, 10));
					if (ptr->addrv4.sin_family == AF_INET) {
						SetWindowText(hUserProtoText, "UDP/IPv4");
						SetWindowText(hUserIPText, inet_ntoa(ptr->addrv4.sin_addr));
					}
					else {
						SetWindowText(hUserProtoText, "UDP/IPv6");
						SetWindowText(hUserIPText, inet_ntop(AF_INET6, &(ptr->addrv6.sin6_addr), ip, 50));
					}
					break;
				}
				EnableWindow(hBanishButton, TRUE);
			}
		}
	}
	return FALSE;
}

// TCP를 처리하는 쓰레드
DWORD WINAPI TCPMain(LPVOID arg) {
	while (isServerStarted == FALSE); // 서버가 시작해야 쓰레드 시작
	int retval;

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	/*----- IPv4 소켓 초기화 시작 -----*/
	// socket()
	listen_sockv4 = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sockv4 == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN serveraddrv4;
	ZeroMemory(&serveraddrv4, sizeof(serveraddrv4));
	serveraddrv4.sin_family = AF_INET;
	serveraddrv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrv4.sin_port = htons(serverPort);
	retval = bind(listen_sockv4, (SOCKADDR*)&serveraddrv4, sizeof(serveraddrv4));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sockv4, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
	/*----- IPv4 소켓 초기화 끝 -----*/

	/*----- IPv6 소켓 초기화 시작 -----*/
	// socket()
	listen_sockv6 = socket(AF_INET6, SOCK_STREAM, 0);
	if (listen_sockv6 == INVALID_SOCKET) err_quit("socket()");

	// bind()
	SOCKADDR_IN6 serveraddrv6;
	ZeroMemory(&serveraddrv6, sizeof(serveraddrv6));
	serveraddrv6.sin6_family = AF_INET6;
	serveraddrv6.sin6_addr = in6addr_any;
	serveraddrv6.sin6_port = htons(serverPort);
	retval = bind(listen_sockv6, (SOCKADDR*)&serveraddrv6, sizeof(serveraddrv6));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	// listen()
	retval = listen(listen_sockv6, SOMAXCONN);
	if (retval == SOCKET_ERROR) err_quit("listen()");
	/*----- IPv6 소켓 초기화 끝 -----*/

	// 데이터 통신에 사용할 변수(공통)
	FD_SET rset;
	SOCKET client_sock;
	int addrlen, i, j;
	// 데이터 통신에 사용할 변수(IPv4)
	SOCKADDR_IN clientaddrv4;
	// 데이터 통신에 사용할 변수(IPv6)
	SOCKADDR_IN6 clientaddrv6;

	while (1) {
		// 소켓 셋 초기화

		FD_ZERO(&rset);
		FD_SET(listen_sockv4, &rset);
		FD_SET(listen_sockv6, &rset);
		for (i = 0; i < nTotalTCPSockets; i++) {
			FD_SET(TCPSocketInfoArray[i]->sock, &rset);
		}
		// select()
		retval = select(0, &rset, NULL, NULL, NULL);
		if (retval == SOCKET_ERROR) {
			err_display("select()");
			break;
		}
		// 소켓 셋 검사(1): 클라이언트 접속 수용(IPv4인 경우)
		if (FD_ISSET(listen_sockv4, &rset)) {
			addrlen = sizeof(clientaddrv4);
			client_sock = accept(listen_sockv4, (SOCKADDR*)&clientaddrv4, &addrlen);

			// 사용자로부터 ID를 받음
			int idLen;
			char userID[120];
			recvn(client_sock, (char*)&idLen, sizeof(idLen), 0);
			retval = recvn(client_sock, userID, idLen, 0);
			userID[retval] = '\0';

			int status = CONNECTION_SUCCESS;

			// ID 중복 확인
			for (int i = 0; i < nTotalTCPSockets; i++) {
				TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
				if (!strcmp(ptr->userID, userID)) {
					status = CONNECTION_FAILED_NICKNAME_DUPLICATED; // 접속 실패 상태 코드
					send(client_sock, (char*)&(status), sizeof(status), 0);
					closesocket(client_sock);
					break;
				}
			}

			for (int i = 0; i < nTotalUDPSockets; i++) {
				UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
				if (!strcmp(ptr->userID, userID)) {
					status = CONNECTION_FAILED_NICKNAME_DUPLICATED; // 접속 실패 상태 코드
					send(client_sock, (char*)&(status), sizeof(status), 0);
					closesocket(client_sock);
					break;
				}
			}

			if (status != CONNECTION_SUCCESS) {
				continue;
			}
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}

			// 접속에 성공했을 경우
			else {
				// 접속한 클라이언트 정보 출력
				send(client_sock, (char*)&(status), sizeof(status), 0);
				DisplayText("%s님이 접속하셨습니다.\r\n", userID);
				SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)userID); // 사용자 목록에 항목 추가
				// 소켓 정보 추가
				AddTCPSocketInfo(client_sock, false, userID);            // TCP 소켓 관리 배열에 항목 추가
			}
		}
		// 소켓 셋 검사(1): 클라이언트 접속 수용(IPv6인 경우)
		if (FD_ISSET(listen_sockv6, &rset)) {
			addrlen = sizeof(clientaddrv6);
			client_sock = accept(listen_sockv6, (SOCKADDR*)&clientaddrv6, &addrlen);
			int idLen;
			char userID[120];
			recvn(client_sock, (char*)&idLen, sizeof(idLen), 0);
			retval = recvn(client_sock, userID, idLen, 0);
			userID[retval] = '\0';

			int status = CONNECTION_SUCCESS;

			// ID 중복 확인
			for (int i = 0; i < nTotalTCPSockets; i++) {
				TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
				if (!strcmp(ptr->userID, userID)) {
					status = CONNECTION_FAILED_NICKNAME_DUPLICATED; // 닉네임 중복 상태 코드
					send(client_sock, (char*)&(status), sizeof(status), 0);
					closesocket(client_sock);
					break;
				}
			}

			for (int i = 0; i < nTotalUDPSockets; i++) {
				UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
				if (!strcmp(ptr->userID, userID)) {
					status = CONNECTION_FAILED_NICKNAME_DUPLICATED; // 닉네임 중복 상태 코드
					send(client_sock, (char*)&(status), sizeof(status), 0);
					closesocket(client_sock);
					break;
				}
			}

			if (status != CONNECTION_SUCCESS) {
				continue;
			}
			if (client_sock == INVALID_SOCKET) {
				err_display("accept()");
				break;
			}
			// 접속에 성공한 경우
			else {
				// 접속한 클라이언트 정보 출력
				send(client_sock, (char*)&(status), sizeof(status), 0);
				DisplayText("%s님이 접속하셨습니다.\r\n", userID);
				SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)userID);
				// 소켓 정보 추가
				AddTCPSocketInfo(client_sock, true, userID);
			}
		}

		while (isBanishing);
		// 소켓 셋 검사(2): 데이터 통신

		for (i = 0; i < nTotalTCPSockets; i++) {
			TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
			if (FD_ISSET(ptr->sock, &rset)) {
				// 데이터 받기(타입)
				retval = recv(ptr->sock, (char*)&(ptr->type), sizeof(ptr->type), 0);
				if (retval <= 0 || ptr->type == -1) {
					RemoveTCPSocketInfo(i, 0);
					continue;
				}
				// 데이터 받기(메시지 크기, 메시지)
				retval = recv(ptr->sock, (char *)&(ptr->bufSize), sizeof(ptr->bufSize), 0);
				ptr->buf = (char*)malloc(ptr->bufSize+1);
				retval = recv(ptr->sock, ptr->buf, ptr->bufSize, 0);
				ptr->buf[retval] = '\0';

				// 받은 메시지가 귓속말인 경우 ("/귓속말"로 시작하는 경우)
				if (!strncmp(ptr->buf, "/귓속말", 7)) {
					// 메시지에서 상대방 ID, 메시지 추출
					char *toID = strtok(ptr->buf + 8, " ");
					if (!toID) {
						sendWhisper(ptr->userID, ptr->userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
						continue;
					}
					int toIDLen = strlen(toID);
					if (toIDLen == 0 || ptr->bufSize <= 9+toIDLen) {
						sendWhisper(ptr->userID, ptr->userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
						continue;
					}
					int msgLen = strlen(ptr->buf + 9 + toIDLen);

					// 귓속말 전송
					sendWhisper(ptr->userID, toID, ptr->buf + 9 + toIDLen, msgLen, WHISPER);
					continue;
				}
				if (retval == 0 || retval == SOCKET_ERROR) {
					RemoveTCPSocketInfo(i, 0);
					continue;
				}
				// 귓속말이 아닌 경우에는 메시지 전송
				sendData(ptr->userID, ptr->buf, ptr->bufSize, ptr->type);
			}
		}
	}

	return 0;
}

// 소켓 정보 추가
BOOL AddTCPSocketInfo(SOCKET sock, bool isIPv6, char* userID)
{
	if (nTotalTCPSockets >= FD_SETSIZE) {
		printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
		return FALSE;
	}

	TCPSOCKETINFO* ptr = new TCPSOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}

	ptr->sock = sock;
	ptr->isIPv6 = isIPv6;
	TCPSocketInfoArray[nTotalTCPSockets++] = ptr;
	strcpy(ptr->userID, userID); // 소켓 정보 구조체에 ID 저장

	// 공지사항, 그림 초기화 버튼 활성화
	EnableWindow(hNoticeButton, TRUE);
	EnableWindow(hNotice, TRUE);
	EnableWindow(hImageResetButton, TRUE);
	return TRUE;
}

// 소켓 정보 삭제
void RemoveTCPSocketInfo(int nIndex, int mode)
{
	isBanishing = TRUE;
	TCPSOCKETINFO* ptr = TCPSocketInfoArray[nIndex];
	int deleteIndex = SendMessage(hUserList, LB_FINDSTRING, -1, (LPARAM)ptr->userID);
	int currentIndex = SendMessage(hUserList, LB_GETCURSEL, 0, 0);
	if (currentIndex != LB_ERR && deleteIndex == currentIndex) {
		// 접속자 정보 텍스트 비우기
		SetWindowText(hUserIDText, "");
		SetWindowText(hUserIPText, "");
		SetWindowText(hUserPortText, "");
		SetWindowText(hUserProtoText, "");
		EnableWindow(hBanishButton, FALSE);
	}
	if (deleteIndex != LB_ERR) {
		SendMessage(hUserList, LB_DELETESTRING, (WPARAM)deleteIndex, 0);
	}

	char userID[IDSIZE];
	strcpy(userID, ptr->userID);

	closesocket(ptr->sock);
	delete ptr;

	if (nIndex != (nTotalTCPSockets - 1))
		TCPSocketInfoArray[nIndex] = TCPSocketInfoArray[nTotalTCPSockets - 1];

	--nTotalTCPSockets;

	if (mode == 0) { // 퇴장
		DisplayText("%s님이 퇴장하셨습니다.\r\n", userID);
	}
	else if (mode == 1) { // 추방
		DisplayText("%s님을 추방하였습니다.\r\n", userID);
	}
	isBanishing = FALSE;
	if (nTotalTCPSockets == 0 && nTotalUDPSockets == 0) {
		// 접속자 수가 0명인 경우 공지사항, 그림 초기화 버튼 비활성화
		EnableWindow(hNoticeButton, FALSE);
		EnableWindow(hNotice, FALSE);
		EnableWindow(hImageResetButton, FALSE);
	}
}

// UDP IPv4 처리 쓰레드
DWORD WINAPI UDPv4Main(LPVOID arg) {
	while (isServerStarted == FALSE); // 서버가 시작해야 쓰레드 시작
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	udp_sockv4 = socket(AF_INET, SOCK_DGRAM, 0);

	SOCKADDR_IN serveraddrv4;
	ZeroMemory(&serveraddrv4, sizeof(serveraddrv4));
	serveraddrv4.sin_family = AF_INET;
	serveraddrv4.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddrv4.sin_port = htons(serverPort);
	int retval = bind(udp_sockv4, (SOCKADDR*)&serveraddrv4, sizeof(serveraddrv4));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	while (1) {
		SOCKADDR_IN peerAddr;
		int addrLen = sizeof(SOCKADDR_IN);
		char userID[IDSIZE];
		int idLen;
		// ID 길이, ID 수신
		retval = recvfrom(udp_sockv4, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&peerAddr, &addrLen);
		if (retval <= 0) {
			continue;
		}
		retval = recvfrom(udp_sockv4, userID, idLen, 0, (SOCKADDR*)&peerAddr, &addrLen);
		if (retval <= 0) {
			continue;
		}
		userID[retval] = '\0';

		// 받은 ID가 UDP_EXIT인 경우 -> 퇴장 처리
		if (!strcmp(userID, "UDP_EXIT")) {
			for (int i = 0; i < nTotalUDPSockets; i++) {
				UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
				if (ptr->isIPv6 == TRUE) continue;
				if (compareAddressv4(ptr->addrv4, peerAddr)) {
					RemoveUDPSocketInfo(i, 0);
					break;
				}
			}
			continue;
		}

		bool isJoined = FALSE, isDuplicated = FALSE;

		// TCP, UDP 소켓 정보 구조체들을 조회하면서 닉네임 중복 검사
		// UDP 소켓을 검사하는데 동일한 IP, 포트 번호, ID를 사용하는 접속자 정보가 있는 경우 -> 이미 접속한 사람(isJoined)
		// UDP 소켓을 검사하는데 ID가 동일하지만 IP, 포트번호가 다른 경우 -> 새로운 접속자(닉네임 중복, isDuplicated)
		for (int i = 0; i < nTotalUDPSockets; i++) {
			UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
			if (!strcmp(ptr->userID, userID)) {
				if (ptr->isIPv6 == TRUE) {
					isDuplicated = TRUE;
				}
				else {
					if (compareAddressv4(ptr->addrv4, peerAddr)) {
						isJoined = TRUE;
					}
					else {
						isDuplicated = TRUE;
					}
				}
			}
		}

		for (int i = 0; i < nTotalTCPSockets; i++) {
			TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
			if (!strcmp(ptr->userID, userID)) {
				isDuplicated = TRUE;
				break;
			}
		}
		while (isBanishing);
		// 아직 접속하지 않은 사람인 경우
		if (!isJoined) {
			int status = CONNECTION_SUCCESS;
			// 닉네임 중복인 경우
			if (isDuplicated == TRUE) {
				status = CONNECTION_FAILED_NICKNAME_DUPLICATED; // 상태 코드(실패, 닉네임 중복)
				retval = sendto(udp_sockv4, (char*)&status, sizeof(status), 0, (SOCKADDR*)&peerAddr, addrLen);
				continue;
			}
			// 아직 접속하지 않은 사람인 경우
			retval = sendto(udp_sockv4, (char*)&status, sizeof(status), 0, (SOCKADDR*)&peerAddr, addrLen);
			DisplayText("%s님이 접속하셨습니다.\r\n", userID);
			SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)userID);
			AddUDPv4SocketInfo(peerAddr, userID); // UDP 접속자 정보 추가
		}

		// 이미 접속한 사람인 경우
		else {
			int type;
			char* msg;
			int msgLen;
			// 타입, 메시지 길이, 메시지 수신
			retval = recvn(udp_sockv4, (char*)&type, sizeof(type), 0);
			retval = recvn(udp_sockv4, (char*)&msgLen, sizeof(msgLen), 0);
			msg = (char*)malloc(msgLen+1);
			retval = recvn(udp_sockv4, msg, msgLen, 0);
			if (retval <= 0) continue;
			msg[retval] = '\0';

			// 귓속말 처리
			if (!strncmp(msg, "/귓속말", 7)) {
				char* toID = strtok(msg + 8, " ");
				if (!toID) {
					sendWhisper(msg, userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
					continue;
				}
				int toIDLen = strlen(toID);
				if (toIDLen == 0 || msgLen <= 9 + toIDLen) {
					sendWhisper(userID, userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
					continue;
				}
				int whisperMsgLen = strlen(msg + 9 + toIDLen);
				sendWhisper(userID, toID, msg + 9 + toIDLen, whisperMsgLen, WHISPER);
				continue;
			}

			// 귓속말이 아닌 경우 메시지 전송
			sendData(userID, msg, msgLen, type);
			free(msg);
		}

	}
}

// UDP IPv6 처리 쓰레드
DWORD WINAPI UDPv6Main(LPVOID arg) {
	while (isServerStarted == FALSE);
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

	udp_sockv6 = socket(AF_INET6, SOCK_DGRAM, 0);

	SOCKADDR_IN6 serveraddrv6;
	ZeroMemory(&serveraddrv6, sizeof(serveraddrv6));
	serveraddrv6.sin6_family = AF_INET6;
	serveraddrv6.sin6_addr = in6addr_any;
	serveraddrv6.sin6_port = htons(serverPort);
	int retval = bind(udp_sockv6, (SOCKADDR*)&serveraddrv6, sizeof(serveraddrv6));
	if (retval == SOCKET_ERROR) err_quit("bind()");

	while (1) {
		SOCKADDR_IN6 peerAddr;
		int addrLen = sizeof(SOCKADDR_IN6);
		char userID[IDSIZE];
		int idLen;
		retval = recvfrom(udp_sockv6, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&peerAddr, &addrLen);
		if (retval <= 0) {
			continue;
		}
		retval = recvfrom(udp_sockv6, userID, idLen, 0, (SOCKADDR*)&peerAddr, &addrLen);
		if (retval <= 0) {
			continue;
		}
		userID[retval] = '\0';
		if (!strcmp(userID, "UDP_EXIT")) {
			for (int i = 0; i < nTotalUDPSockets; i++) {
				UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
				if (ptr->isIPv6 == FALSE) continue;
				if (compareAddressv6(ptr->addrv6, peerAddr)) {
					RemoveUDPSocketInfo(i, 0);
					break;
				}
			}
			continue;
		}

		bool isJoined = FALSE, isDuplicated = FALSE;
		for (int i = 0; i < nTotalUDPSockets; i++) {
			UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
			if (!strcmp(ptr->userID, userID)) {
				if (ptr->isIPv6 == TRUE) {
					if (compareAddressv6(ptr->addrv6, peerAddr)) {
						isJoined = TRUE;
					}
					else {
						isDuplicated = TRUE;
					}
				}
				else {
					isDuplicated = TRUE;
				}
				break;
			}
		}

		for (int i = 0; i < nTotalTCPSockets; i++) {
			TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
			if (!strcmp(ptr->userID, userID)) {
				isDuplicated = TRUE;
				break;
			}
		}
		while (isBanishing);
		if (!isJoined) {
			int status = CONNECTION_SUCCESS;
			if (isDuplicated == TRUE) {
				status = CONNECTION_FAILED_NICKNAME_DUPLICATED;
				retval = sendto(udp_sockv6, (char*)&status, sizeof(status), 0, (SOCKADDR*)&peerAddr, addrLen);
				continue;
			}
			retval = sendto(udp_sockv6, (char*)&status, sizeof(status), 0, (SOCKADDR*)&peerAddr, addrLen);
			DisplayText("%s님이 접속하셨습니다.\r\n", userID);
			SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)userID);
			AddUDPv6SocketInfo(peerAddr, userID);
		}
		else {
			int type;
			char* msg;
			int msgLen;
			retval = recvn(udp_sockv6, (char*)&type, sizeof(type), 0);
			retval = recvn(udp_sockv6, (char*)&msgLen, sizeof(msgLen), 0);
			msg = (char*)malloc(msgLen + 1);
			retval = recvn(udp_sockv6, msg, msgLen, 0);
			if (retval <= 0) continue;
			msg[retval] = '\0';
			if (!strncmp(msg, "/귓속말", 7)) {
				char* toID = strtok(msg + 8, " ");
				if (!toID) {
					sendWhisper(msg, userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
					continue;
				}
				int toIDLen = strlen(toID);
				if (toIDLen == 0 || msgLen <= 9 + toIDLen) {
					sendWhisper(userID, userID, whisperErrorMessage, strlen(whisperErrorMessage), WARNING);
					continue;
				}
				int whisperMsgLen = strlen(msg + 9 + toIDLen);
				sendWhisper(userID, toID, msg + 9 + toIDLen, whisperMsgLen, WHISPER);
				continue;
			}
			sendData(userID, msg, msgLen, type);
			free(msg);
		}

	}
}

// UDP(IPv4) 접속자 정보 추가 함수
BOOL AddUDPv4SocketInfo(SOCKADDR_IN addr, char* userID)
{
	if (nTotalUDPSockets >= FD_SETSIZE) {
		printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
		return FALSE;
	}

	UDPSOCKETINFO* ptr = new UDPSOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}

	ptr->isIPv6 = FALSE;
	ptr->addrv4 = addr;

	UDPSocketInfoArray[nTotalUDPSockets++] = ptr;
	strcpy(ptr->userID, userID);

	// 공지사항, 그림 초기화 버튼 활성화
	EnableWindow(hNoticeButton, TRUE);
	EnableWindow(hNotice, TRUE);
	EnableWindow(hImageResetButton, TRUE);
	return TRUE;
}

// UDP(IPv6) 접속자 정보 추가 함수
BOOL AddUDPv6SocketInfo(SOCKADDR_IN6 addr, char* userID)
{
	if (nTotalUDPSockets >= FD_SETSIZE) {
		printf("[오류] 소켓 정보를 추가할 수 없습니다!\n");
		return FALSE;
	}

	UDPSOCKETINFO* ptr = new UDPSOCKETINFO;
	if (ptr == NULL) {
		printf("[오류] 메모리가 부족합니다!\n");
		return FALSE;
	}

	ptr->isIPv6 = TRUE;
	ptr->addrv6 = addr;

	UDPSocketInfoArray[nTotalUDPSockets++] = ptr;
	strcpy(ptr->userID, userID);
	EnableWindow(hNoticeButton, TRUE);
	EnableWindow(hNotice, TRUE);
	EnableWindow(hImageResetButton, TRUE);
	return TRUE;
}

// UDP(IPv4) 접속자 정보 제거 함수
void RemoveUDPSocketInfo(int nIndex, int mode)
{
	UDPSOCKETINFO* ptr = UDPSocketInfoArray[nIndex];
	int deleteIndex = SendMessage(hUserList, LB_FINDSTRING, -1, (LPARAM)ptr->userID);
	int currentIndex = SendMessage(hUserList, LB_GETCURSEL, 0, 0);
	if (currentIndex != LB_ERR && deleteIndex == currentIndex) {
		// 접속자 정보 지우기
		SetWindowText(hUserIDText, "");
		SetWindowText(hUserIPText, "");
		SetWindowText(hUserPortText, "");
		SetWindowText(hUserProtoText, "");
	}
	if (deleteIndex != LB_ERR) {
		SendMessage(hUserList, LB_DELETESTRING, (WPARAM)deleteIndex, 0);
	}
	char userID[IDSIZE];
	strcpy(userID, ptr->userID);

	delete ptr;

	if (nIndex != (nTotalUDPSockets - 1))
		UDPSocketInfoArray[nIndex] = UDPSocketInfoArray[nTotalUDPSockets - 1];

	--nTotalUDPSockets;
	if (mode == 0) {
		DisplayText("%s님이 퇴장하셨습니다.\r\n", userID);
	}
	else if (mode == 1) {
		DisplayText("%s님을 추방하였습니다.\r\n", userID);
	}

	// 서버에 남아 있는 사람이 없는 경우 공지, 그림 초기화 버튼 비활성화
	if (nTotalTCPSockets == 0 && nTotalUDPSockets == 0) {
		EnableWindow(hNoticeButton, FALSE);
		EnableWindow(hNotice, FALSE);
		EnableWindow(hImageResetButton, FALSE);
	}
}

// 소켓 함수 오류 출력
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

void DisplayText(char* fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);

	char cbuf[1024];
	vsprintf(cbuf, fmt, arg);

	int nLength = GetWindowTextLength(hUserLog);
	SendMessage(hUserLog, EM_SETSEL, nLength, nLength);
	SendMessage(hUserLog, EM_REPLACESEL, FALSE, (LPARAM)cbuf);

	va_end(arg);
}

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

// 모든 접속자에게 메시지를 전송하는 함수
void sendData(char* userID, char* data, int dataLen, int type) {
	int retval;
	int idLen = strlen(userID);
	for (int i = 0; i < nTotalTCPSockets; i++) {
		// TCP 접속자들에게 ID 길이, ID, 메시지 타입, 데이터 길이, 데이터 전송
		TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
		retval = send(ptr->sock, (char*)&idLen, sizeof(idLen), 0);
		retval = send(ptr->sock, userID, idLen, 0);
		retval = send(ptr->sock, (char*)&type, sizeof(type), 0);
		retval = send(ptr->sock, (char*)&dataLen, sizeof(dataLen), 0);
		retval = send(ptr->sock, data, dataLen, 0);
	}
	for (int i = 0; i < nTotalUDPSockets; i++) {
		UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
		// UDP 접속자들에게 ID 길이, ID, 메시지 타입, 데이터 길이, 데이터 전송
		if (ptr->isIPv6 == TRUE) {
			retval = sendto(udp_sockv6, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
			retval = sendto(udp_sockv6, userID, idLen, 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
			retval = sendto(udp_sockv6, (char*)&type, sizeof(type), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
			retval = sendto(udp_sockv6, (char*)&dataLen, sizeof(dataLen), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
			retval = sendto(udp_sockv6, data, dataLen, 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
		}
		else {
			retval = sendto(udp_sockv4, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
			retval = sendto(udp_sockv4, userID, idLen, 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
			retval = sendto(udp_sockv4, (char*)&type, sizeof(type), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
			retval = sendto(udp_sockv4, (char*)&dataLen, sizeof(dataLen), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
			retval = sendto(udp_sockv4, data, dataLen, 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
		}

	}
}

// 귓속말 전송 함수
void sendWhisper(char* userID, char* toID, char *data, int dataLen, int type) {
	int retval;
	int idLen = strlen(userID);
	for (int i = 0; i < nTotalTCPSockets; i++) {
		TCPSOCKETINFO* ptr = TCPSocketInfoArray[i];
		// TCP를 사용하는 귓속말 대상에게 ID 길이, ID, 메시지 타입, 데이터 길이, 데이터 전송
		if (!strcmp(toID, ptr->userID)) {
			retval = send(ptr->sock, (char*)&idLen, sizeof(idLen), 0);
			retval = send(ptr->sock, userID, idLen, 0);
			retval = send(ptr->sock, (char*)&type, sizeof(type), 0);
			retval = send(ptr->sock, (char*)&dataLen, sizeof(dataLen), 0);
			retval = send(ptr->sock, data, dataLen, 0);
			return;
		}
	}
	for (int i = 0; i < nTotalUDPSockets; i++) {
		UDPSOCKETINFO* ptr = UDPSocketInfoArray[i];
		// UDP를 사용하는 귓속말 대상에게 ID 길이, ID, 메시지 타입, 데이터 길이, 데이터 전송
		if (ptr->isIPv6 == TRUE) {
			if (!strcmp(toID, ptr->userID)) {
				retval = sendto(udp_sockv6, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
				retval = sendto(udp_sockv6, userID, idLen, 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
				retval = sendto(udp_sockv6, (char*)&type, sizeof(type), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
				retval = sendto(udp_sockv6, (char*)&dataLen, sizeof(dataLen), 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
				retval = sendto(udp_sockv6, data, dataLen, 0, (SOCKADDR*)&(ptr->addrv6), sizeof(ptr->addrv6));
				return;
			}
		}
		else {
			if (!strcmp(toID, ptr->userID)) {
				retval = sendto(udp_sockv4, (char*)&idLen, sizeof(idLen), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
				retval = sendto(udp_sockv4, userID, idLen, 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
				retval = sendto(udp_sockv4, (char*)&type, sizeof(type), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
				retval = sendto(udp_sockv4, (char*)&dataLen, sizeof(dataLen), 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
				retval = sendto(udp_sockv4, data, dataLen, 0, (SOCKADDR*)&(ptr->addrv4), sizeof(ptr->addrv4));
				return;
			}
		}
	}

	// 존재하지 않는 ID인 경우, 귓속말을 보냈던 사람에게 경고 메시지 전송
	sendWhisper(userID, userID, whisperNoIDMessage, sizeof(whisperNoIDMessage), WARNING);
}

// IP, 포트번호 비교 함수 (IPv4) -> 새로 접속한 사람인지, 이미 접속한 사람인지 구분하기 위함
BOOL compareAddressv4(SOCKADDR_IN addr1, SOCKADDR_IN addr2) {
	char s_addr1[50], s_addr2[50];
	DWORD s_addr1len = sizeof(s_addr1), s_addr2len = sizeof(s_addr2);
	WSAAddressToString((SOCKADDR*)&addr1, sizeof(SOCKADDR_IN), NULL, s_addr1, &s_addr1len);
	WSAAddressToString((SOCKADDR*)&addr2, sizeof(SOCKADDR_IN), NULL, s_addr2, &s_addr2len);
	return !strcmp(s_addr1, s_addr2);
}

// IP, 포트번호 비교 함수 (IPv6) -> 새로 접속한 사람인지, 이미 접속한 사람인지 구분하기 위함
BOOL compareAddressv6(SOCKADDR_IN6 addr1, SOCKADDR_IN6 addr2) {
	char s_addr1[50], s_addr2[50];
	DWORD s_addr1len = sizeof(s_addr1), s_addr2len = sizeof(s_addr2);
	WSAAddressToString((SOCKADDR*)&addr1, sizeof(SOCKADDR_IN6), NULL, s_addr1, &s_addr1len);
	WSAAddressToString((SOCKADDR*)&addr2, sizeof(SOCKADDR_IN6), NULL, s_addr2, &s_addr2len);
	return !strcmp(s_addr1, s_addr2);
}