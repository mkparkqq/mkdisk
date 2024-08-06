# 테스트 스크립트 설명

## unittest.sh

`$ ./unittest.sh`

`module` 디렉토리의 `queue.c` `list.c` `hashmap.c` 에 대한 테스트를 실행한다.

## run_test_server.sh

`$ ./run_test_server.sh`

* `server.out` 프로그램을 백그라운드에서 실행시키고 표준출력을 `server_test.log`로 리다이렉션한다.
* 상위 디렉토리의 `server.out`이 빌드되어 있어야 한다.

## client.test

`$ ./client.test [서버 ip] [서버 port] [파일 이름] [0|1]`

* `make` 명령어로 생성
* 아래의 테스트를 진행하기 위해 필요한 클라이언트 프로그램

## upload_test.sh

`$ ./upload_test.sh [업로드할 파일] [횟수] [0|1]`

* 전달받은 파일을 이름을 바꿔가며 업로드한다. 
* 각 업로드마다 클라이언트가 서버에 접속하고 종료된다.
* 서버의 주소는 하드코딩되어있다

## parallel_upload_test.sh

`$ ./upload_test.sh [업로드할 파일] [n] [m]`

* n개의 스레드에서 m개의 파일을 전송한다. 
* 각 요청마다 클라이언트가 서버에 접속하고 종료한다.
* ` 내부적으로 'upload_test.sh' 스크립트를 사용한다.
