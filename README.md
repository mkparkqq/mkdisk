# mkdisk

Tibero onboarding project

## 서버가 제공하는 서비스

* 업로드된 파일 목록 조회
* 파일 업로드
	* 파일을 업로드할 때 접근 권한(PUBLIC/PRIVATE)을 설정할 수 있다.
* 파일 다운로드

## 서버가 관리하는 상태

<img src="/img/status-diagram.png" alt="status-diagram" />

* g_inven_cache.items
	* 서버에 업로드된 파일들에 대한 정보를 저장(캐싱)하는 배열
	* 프로그램이 초기화될때 `MAX_FILE_ITEMS`만큼의 공간을 할당받는다
* g_inven_cache.nametb
	* 이미 저장된 파일의 이름과 해당 파일 정보가 캐싱된 items 배열의 인덱스 대응 관게를 저장한다
	* [struct queue](https://github.com/mkparkqq/mkdisk/blob/main/module/hashmap.h)를 사용하여 구현
	* 동기화 매커니즘이 내재되어 있다
* g_inven_cache.fidq
	* items 배열 중 사용 가능한 공간의 인덱스를 제공한다.
	* [배열 기반 큐(struct queue)](https://github.com/mkparkqq/mkdisk/blob/main/module/queue.c)로 프로그램이 초기화될때 `MAX_FILE_ITEMS` 만큼의 공간이 할당되고 원소가 추가된다
	* 동기화 매커니즘이 내재되어 있다
* g_inven_cache.ilock
	* g_inven_cache.items 배열을 스레드로부터 보호하는 rwlock(`pthread_rwlock_t`).
* g_sworker_pool
	* 클라이언트의 세션(요청)을 처리하는 스레드(worker)들이 저장된 배열.
	* 각 스레드의 tid, pipefd(읽기 전용), dpipefd(쓰기 전용)가 저장된다.
		* pipefd : 메인 스레드가 worker로 요청이 들어온 클라이언트의 소켓 디스크립터를 전달한다.
		* dpipefd : worker가 작업을 끝냈음을 메인 스레드에게 알려준다.
	* 프로그램이 초기화될때 `MAX_SESSIONS` 개의 스레드가 생성된다.
* g_sworkerid_queue
	* 놀고 있는 worker의 인덱스를 제공한다.
* g_eppollfd
	* 세 가지의 이벤트에 대해 메인 스레드를 깨운다 (epoll_wait 반환)
	* EVENT_NEW_SESSION - 클라이언트 접속
	* EVENT_SERVICE_REQUEST - 클라이언트의 요청(업로드, 다운로드)
		* worker에서 처리되는 동안 메인 스레드로 동일한 이벤트가 전달되지 않도록 EPOLLONESHOT 옵션 사용
	* EVENT_WORKER_MSG - worker의 작업이 끝남


## 동시에 여러 스레드의 요청을 처리하는 방식

놀고 있는 **공유 스레드**가 클라이언트의 요청(세션) 단위로 할당되어 요청을 처리한다.

### 장점
* 접속만 하고 요청을 보내지 않는 클라이언트에게 스레드가 낭비되지 않는다.
### 단점
* 요청을 보내는 사람이 많을 경우 접속에 성공했지만 요청은 실패(timeout 5초)할 수 있다.

## worker가 처리해야 할 작업 정보를 전달받는 방식

메인 스레드가 **특정** worker 한 개를 할당하고 회수함으로써 경합을 최대한 회피

<img src="/img/request-handling.png" alt="request-handling" />

* main thread
	* 클라이언트의 접속을 처리 (클라이언트의 소켓을 epoll instance에 등록)
	* 놀고 있는 스레드에게 이벤트(클라이언트 요청)를 전달한다.
	* 스레드가 작업을 완료하면 해당 스레드의 wid를 g_sworkerid_queue에 삽입하고 스레드가 처리한 이벤트를 다시 g_epollfd에 등록한다(EPOLLONESHOT은 한 번 이벤트가 전달되면 이후에는 비활성화되어 해당 클라이언트가 다른 요청을 보냈을때 epoll_wait이 반환되지 않는다).

* worker thread
	* 메인 스레드가 pipefd에 sockfd를 write하면 worker가 깨어나 해당 클라이언트와 통신한다.
	* 클라이언트의 요청을 처리한 뒤 dpipefd에 자신의 wid를 write하여 작업이 끝났음을 알린다.

### 대안

* 메인스레드가 작업 큐에 작업 정보를 저장한 뒤 semaphore로 **임의의 worker**를 한 개 깨우는 방식
	* 여러 스레드가 한 번에 깨어난 경우(클라이언트 요청이 동시에 들어온 경우) 작업 큐에서 worker들과 main 스레드의 경합 발생
	* 스레드가 자신의 작업이 끝났음을 알리기 위한 큐를 생성하면 해당 큐에서 경합 발생
* 소켓을 사용하지 않고`pthread_cond_signal`로 **특정** worker를 깨우는 방식
	* 작업을 전달하는 데에는 경합을 피할 수 있지만 작업이 끝났음을 메인 스레드에게 알리는 과정은 위의 대안과 동일

## module

### server_service

[코드](https://github.com/mkparkqq/mkdisk/blob/main/server_service.c)

서버가 제공하는 서비스에 호출되는 함수들을 묶어놓은 모듈. `module/service.h`에 선언된 함수들 중 서버에서 사용되는 함수들이 정의되어 있다.

### sockutil

[코드](https://github.com/mkparkqq/mkdisk/blob/main/module/sockutil.c)

* 서버와 클라이언트가 공통적으로 사용하는 소켓 관련 시스템 콜을 감싼 api.
* 반환값을 `sockutil_errstr`에 인자로 넣고 호출하면 에러에 대한 설명(문자열 리터럴 주소)을 반환한다.
* 반환형이 void인 함수는 실패하지 않는다.

### fileutil

[코드](https://github.com/mkparkqq/mkdisk/blob/main/module/fileutil.c)

* 서버와 클라이언트가 공통적으로 사용하는 파일 관련 시스템 콜 api
* 정수형을 반환하는 함수의 반환값을 `futil_errstr` 함수에 전달하면 에러에 대한 설명 문자열을 반환한다.

### hashmap

* 삽입되는 데이터는 shallow copy된다. 
* 동적 할당된 공간에 대한 주소를 삽입한 경우 `destruct_hashmap`과 별개로 직접 해제 해야 한다.

<img src="/img/hashmap.png" alt="hashmap" />

### queue

* 삽입되는 원소는 deep copy된다.
* 동적할당된 공간에 대한 주소를 삽입 직후 free하거나 스택 공간에 대한 주소를 삽입할 수 있다.

<img src="/img/queue.png" alt="queue" />
