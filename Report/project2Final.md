# CSED 312 Project 2 Final Report 
김지현(20220302) 
전현빈(20220259)

## Overall Project Design 

이번 프로젝트 2에서는 아래의 네 가지 task가 있다. 각각의 task를 실행하기 위해 구현한 함수들을 설명하기 전에, 전체적인 User Program의 작동 방식에 대해 설명하겠다. 부모와 자식 프로세스 간의 관계를 저장하고 각 프로세스가 사용하는 리소스(추후 설명될 파일 포함)를 관리하기 위해 pcb(process control block)에 해당하는 구조체를 새롭게 정의하였다.

```
/src/threads/thread.h
pcb structure
```

pcb 구조체는 프로세스 제어 블록으로, 각 프로세스의 상태와 관련된 정보를 저장하기 위해 설계되었다. 이 구조체는 프로세스 ID, 부모-자식 관계, 리소스 사용 상태 등을 저장하며, 이 정보는 각 프로세스가 종료될 때까지 관리된다. (추가)

```
/src/threads/thread.h
file structure
```

위에서 언급한 프로세서가 사용하는 리소스 중 하나인 파일을 관리하기 위해, 별도의 file 구조체를 정의하였다.  
file 구조체는 파일의 포인터와 파일의 현재 상태를 관리하며, 이를 통해 각 프로세스는 안전하게 파일에 접근할 수 있게 된다. (추가)

```
/src/threads/thread.h
thread 구조에 어떻게 구현되어있는지
```

위에서 설명한 pcb 구조체와 기타 추가 요소들이 각 thread 객체가 가질 수 있도록 선언하였다. 이때, user program을 실행할 때 위의 요소들이 필요하기 때문에 `#ifdef`를 사용해 `userprog` 매크로가 설정되었을 때만 생성되도록 구현하였다.

```
/src/threads/thread.c
thread create 에서 어떻게 initialize 되어있는지 
```

그리고 앞서 설명한 것처럼, thread 구조체에서 정의된 새로운 객체들을 `thread_create` 함수에서 초기화하였다.  
각 변수/객체들이 어떻게 초기화되었는지에 대한 상세한 내용은 다음과 같다. (추가) 

## 1. Argument Passing 

### Implementation & Improvement from the previous design 

첫 번째 과제는 argument passing이다. argument는 어떤 함수를 호출할 때 넘겨주는 인자로, 기존의 `process_execute` 함수는 argument(인자)를 넘겨주지 않는 방식으로 구현되어 있다. 그러나 Pintos 문서에 설명된 바와 같이 "grep foo bar"와 같은 명령어를 실행하려면 "foo"와 "bar"라는 인자를 넘겨주면서 "grep" 함수를 호출해야 한다. 이를 위해, 명령어를 " "(공백)을 기준으로 parsing해야 한다.

따라서 `process_execute` 함수에서 위의 명령어를 parsing하는 함수를 추가하였다.  
(또한, `process_execute` 함수에서 앞서 설명한 `thread_create` 함수를 호출하며, 이 함수의 인자 중 하나로 `start_process`가 넘겨지는데, 이 함수에 대한 설명은 아래에 이어서 설명할 것이다.)

```
/src/userprog/process.c & h 
argument parsing function 추가
```

위 함수에 대한 설명 (추가)

이때, Pintos 문서에 설명된 'lib/string.h' 파일의 `strtok_r` 함수를 사용하여 구현하였다. 각 인자는 `char** argv`에 저장되며, `int argc`는 인자의 총 개수를 나타낸다.

```
/src/userprog/process.c
start process function 
```

`start_process` 함수는 user program을 새로운 process로 로드하고 실행하는 역할을 한다.  
`start_process` 자체에서 수정한 내용 (추가)

이때 프로그램을 실행할 때 필요한 인자를 user stack에 추가해야 하며, 이 역할을 수행하는 함수를 새롭게 추가하였다.

```
user stack function
```

위 함수를 보면, user stack에 순서대로 값을 넣는 것을 확인할 수 있다.

user stack 내부 순서 사진 (추가)  
순서와 user stack이 아래로 자라나게 만드는 이유 (추가)

이렇게 구현을 완료하면 첫 번째 과제인 argument passing에서 요구하는 목적을 달성할 수 있다.

### Difference from design report

`strtok_r` 함수를 사용하여 구현한다는 것은 랩 시간과 Pintos 문서에서도 설명되었기 때문에 명령어 parsing 과정을 어려움 없이 진행할 수 있었다. 그러나 `start_process` 함수에서 앞서 parsing한 인자들과 인자 개수, return address 등을 user stack에 추가하는 함수에 대한 구체적인 설계는 하지 않았다. 하지만 수업에서 배운 것처럼 user stack은 아래로 커지며 (address 적으로 설명 (추가)) 어떤 순서로 값이 저장되어야 하는지에 대한 개념을 바탕으로 구현을 완료하였다.


## 2. System Calls (추가) 함수 이름 수정 필요 !! (추가) 

### Implementation & Improvement from the previous design 

두 번째 과제는 system call을 구현하는 것이었다. system call은 user program이 OS 기능에 접근하기 위해 사용하는 함수들로 크게 두 가지로 나눌 수 있다. process 작동에 대한 기능들과 파일 접근에 대한 기능이다.  
먼저 process 작동에 대한 기능으로는 `halt`, `exit`, `exec`, 그리고 `wait`가 있으며, 파일 접근에 대한 기능으로는 `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, 그리고 `close`가 있다.

```
/src/userprog/syscall.c
halt
```

먼저 `halt`는 Pintos 문서에 설명된 대로 `shutdown_power_off` 함수를 호출하는 동작만 수행하면 된다.  
이 함수에 대한 설명과 언제 사용하게 되는지에 대한 설명 (추가)

```
exit
```

`exit` 함수는 현재 실행 중인 user program을 종료하고 kernel에 `status`를 반환하는 함수이다. 현재 process의 부모 process가 `wait` 중이라면 반환되는 것은 이 `status`일 것이다. 즉, `status`는 `exit` 코드와 같다. 보통 `0`은 현재 process가 성공적으로 종료되었음을 나타내며, `0`이 아니라면 오류가 발생했음을 알 수 있다. 해당 함수는 `thread_exit` 함수 호출을 통해 실제로 process를 종료시킬 수 있다.

```
exec
```

`exec`는 `cmd_line`으로 받은 `name`을 가진 executable을 실행시키는 함수로, 새로운 process의 `pid`(program id)를 반환한다. 해당 프로그램이 실행되지 못하면 `-1`을 반환한다.

```
wait
```

부모 process가 자식 process가 종료될 때까지 기다리며 child의 `exit status`를 저장하고 `pid`를 반환받는 함수이다. 만약 자식 process가 종료되지 않았는데 kernel에 의해 종료되었다면 `-1`을 반환하여 부모 process가 `exit status`를 받을 수 있도록 한다. 이렇게 구현하면 kernel에 의해 자식 process가 종료되었음을 확인할 수 있다.

Pintos 문서 설명 (추가)

이후의 함수들은 파일 접근에 관한 기능들이다.

```
create
```

`create` 함수는 "file"이라는 이름과 "initial_size" 크기를 가진 새로운 파일을 생성하는 함수이다. 새로운 파일 생성 성공 여부에 따라 boolean 값을 반환한다. 파일을 생성하는 것과 파일을 open하는 것은 다르며, 이는 아래의 `open` 함수에서 설명하겠다.

```
remove
```

`remove` 함수는 파일이 열려 있는지 여부와 상관없이 "file" 이름을 가진 파일을 삭제하는 역할을 한다. 그리고 파일 삭제 성공 여부를 반환한다. 열려 있는 파일을 삭제하더라도 파일을 close하지는 않는다.

```
open
```

`open` 함수는 "file"이라는 이름을 가진 파일을 여는 역할을 한다. 파일이 열리면 file descriptor에 해당하는 non-negative integer를 반환하고, 열리지 않으면 `-1`을 반환한다. 여기서 file descriptor는 standard input 파일일 때 `0`, standard output 파일일 때 `1`을 갖는다. 각 process가 파일을 열 때마다 새로운 file descriptor number가 할당되며, 이 file descriptor는 각 process에 따라 존재하며 자식 process에는 전달되지 않는다. 또한, 동일한 파일을 여러 번 open하면 새로운 file descriptor가 반환되기 때문에 각 file descriptor를 따로 close해 주어야 한다.

```
filesize
```

`filesize` 함수는 `fd`라는 file descriptor를 가진 파일의 크기를 bytes 단위로 반환한다.

```
read
```

`read`는 `fd`라는 file descriptor를 가진 파일에서 "size" bytes만큼 읽어서 buffer에 저장하는 역할을 한다. 실제로 읽은 bytes 수를 반환하며, 만약 읽어올 수 없다면 `-1`을 반환한다. 예를 들어 `fd`가 `0`인 경우, 이는 standard input이므로 `input_getc()` 함수를 이용해 키보드 입력을 읽어온다.

```
write
```

`write` 함수는 `fd`라는 file descriptor를 가진 파일에 "size" bytes만큼 buffer에 저장된 내용을 작성하는 역할을 한다. 그리고 실제로 작성된 bytes 수를 반환한다. 일반적으로 기존 파일 크기보다 더 많은 내용을 작성할 경우 파일 크기가 늘어나지만, Pintos의 기본 파일 시스템은 fixed size file로 구현되어 있어, end of file(eof)에 도달한 후까지 작성된 바이트 수를 반환하도록 한다. 예를 들어 `fd`가 `1`일 경우, 이는 standard output이므로 `put_buf()` 함수를 이용해 콘솔에 buffer에 저장된 값을 출력한다.

```
seek
```

`seek` 함수는 `fd`라는 file descriptor를 가진 파일을 읽거나 쓸 때 시작 위치를 `position`으로 변경하는 역할을 한다. `position`이 `0`이라면 파일의 시작점을 가리킨다. 만약 파일의 eof 이후의 위치를 가리킨다면 `read` 시 `0`을 반환하고 `write` 시에는 위에서 설명한 이유로 오류를 발생시킬 것이다. 따라서 `position` 위치를 확인하는 과정이 필요하다. (추가)

```
tell
```

`tell` 함수는 `fd`라는 file descriptor를 가진 파일의 다음 읽기 또는 쓰기 위치인 next byte의 위치를 반환한다.

```
close
```

마지막으로 `close` 함수는 `fd`라는 file descriptor를 가진 파일을 닫는 역할을 한다. process가 종료될 때 자동으로 모든 열려 있는 file descriptor들이 이 함수 호출을 통해 닫힌다.

```
address valid 한지 확인하는 함수
```

위에서 설명한 system call을 구현하기 위해 사용된 함수 중 하나로, 주어진 주소가 user 영역에 속하는지 확인하고 boolean 값을 반환하는 함수이다. 여기서 사용된 값들은 `syscall.h`에 나와 있는 base 값이다. (추가) -- 정확히 어떤 define으로 적혀 있는지 확인 필요

```
argument 저장 함수
```

위에 설명된 system call 함수들을 보면 인자의 개수가 다르다. 각 함수에서 올바른 인자를 넘겨주기 위해 user stack에 있는 값을 인자 개수만큼 가져와 kernel에 저장해야 한다. 이 함수는 해당 역할을 하며, 아래에서 설명할 switch-case 구조에서 각 system call 별로 호출될 예정이다.

```
syscall handler
```

이 함수는 switch-case 구조를 통해 각 system call을 구현하는 함수이다. 위에서 설명한 argument를 pop 해오는 함수 호출을 통해 인자를 argv stack에 저장하고, 위의 system call 함수 호출을 통해 handling한다. 반환값이 있는 경우, `f->eax`에 저장하도록 하였다.


### Difference from design report 
```
현재 syscall_handler는 바로 종료하는 방식으로 구현되어 있으므로, 이 부분을 우선적으로 수정해야 한다. 우선 intr_frame의 esp 값을 읽어와, 스택에 사용자 프로그램이 push한 인자들과 시스템 호출 번호를 4바이트 단위로 읽어들여 이를 스택 또는 큐에 복사해 저장한다. 이후 시스템 호출에 필요한 정보를 syscall_handler에 저장하여 처리할 수 있도록 한다.

그 후 syscall_handler에 각 시스템 호출 번호에 해당하는 헬퍼 함수를 정의하고 구현하여, 각 번호에 맞는 시스템 호출이 적절한 헬퍼 함수에서 처리되도록 구현한다. 예를 들어, sys_exit_helper와 같은 헬퍼 함수를 통해 시스템 호출 번호와 인자를 전달하고, 반환된 값을 intr_frame의 eax에 저장할 수 있도록 한다. 이때 적절한 헬퍼함수의 구현은 아래 4번 file system관련해서 구현해야하는 함수와 중복된다. 예를들어 file creation과 관련된 syscall을 수행하는 함수의 경우 파일 시스템 함수를 그대로 사용하면 될것이다.

또한, 시스템 호출 및 실행 파일 쓰기 제한을 위한 pcb(프로세스 제어 블록) 구조체를 새롭게 정의하고자 한다. pcb에는 프로세스의 고유 ID, 부모 프로세스, 파일 시스템 접근을 위한 플래그 변수, 대기 처리를 위한 변수, 현재 실행 중인 파일이 포함되어야 한다. 그리고 thread 구조체에 이 pcb 포인터를 추가한 뒤, 프로세스가 생성되고 시작될 때(start_process 시점)에 해당 스레드의 pcb를 초기화할 예정이다. 여기서 추가된 현재 실행 중인 파일을 가리키는 포인터는 아래의 4번 과제에서 더 자세히 설명할 예정이다.
```

기존의 design report 내용과 유사함. (디테일만 추가 됨) 


## 3. Process Termination Messages 

### Implementation & Improvement from the previous design 

이 과제에서는 user process가 종료될 때, process의 이름과 exit code를 출력하도록 구현하는 것이 목적이다. 이는 Pintos 문서에 설명된 대로 `exit` 함수를 호출할 때 `printf(...)`를 추가하여 구현할 수 있다. 따라서 위에서 설명한 syscall `exit` 함수에 아래의 라인을 추가하였다. (추가) -- 함수 이름 수정 필요

```
syscall exit
```

### Difference from design report

기존에는 `thread` 구조체에 종료 여부를 확인할 수 있는 boolean 변수를 추가하는 방식을 고려했으나, `exit`을 처리하는 함수에서 바로 `printf`를 실행해도 충분히 과제를 수행할 수 있음을 syscall을 구현하면서 깨달았다. 이에 따라 위와 같이 수정하였다.


## 4. Denying Writes to Executables 

### Implementation & Improvement from the previous design 

마지막 과제는 열려 있는 파일에 쓰기 작업을 하지 않도록 하는 것이다. Pintos 문서에 나타난 대로 파일을 열 때 `file_deny_write` 함수를 함께 호출하여 쓰기를 제한하고, 파일을 닫을 때 `file_allow_write` 함수를 이용해 쓰기를 허용하는 과정을 추가하면 된다.

``` file_deny_write 이랑 file_allow_write 함수 설명 추가해야 하는지? 디자인 레포트에서 설명하긴 했는데..  (추가) ```

```
load
```

먼저 `load` 함수에서 `filesys_open` 함수를 호출하기 때문에, 파일이 성공적으로 열렸다면 `file_deny_write` 함수를 호출하여 쓰기를 할 수 없도록 한다.

```
syscall open
```

또한, 2번 과제에서 구현한 syscall 중 `open` 함수에서도 `filesys_open` 함수를 호출하므로, 파일이 성공적으로 열렸다면 `file_deny_write` 함수를 호출하여 쓰기를 제한한다.

```
file_close
```

2번 과제의 syscall 함수 중 `close` 함수에서 `file_close` 함수를 통해 파일을 닫는 동작이 이미 포함되어 있는데, 해당 함수를 살펴보면 `file_allow_write`가 이미 포함되어 있어 추가할 필요가 없다. 따라서 `load`와 `syscall open` 함수를 수정하는 것으로 이번 과제를 마무리할 수 있다.

### Difference from design report

기존에는 `load` 함수에서 `file_deny_write` 함수를 호출하여 쓰기를 제한하고, 파일을 닫을 때가 아닌 `process_exit` 함수가 호출될 때 `file_allow_write` 함수를 호출하여 다시 쓰기를 허용하는 방식으로 설계하였다. 그러나 syscall을 구현하면서 `syscall_open` 함수에서도 파일을 열어 쓰기를 제한하는 과정이 필요하며, 반대로 `process_exit` 대신 `file_close` 함수에서 이미 `file_allow_write` 함수를 사용하므로 별도로 쓰기 허용 작업을 추가하지 않아도 된다는 점을 알게 되어 이와 같이 수정하였다. 

### Overall Limitations 
-> write 함수에서 파일 늘어나는거 구현안된거

file 닫는거 헷갈렸다는 점 추가 

스케줄링 문제? 

### Overall Discussion 


# Result 

(결과 사진) (추가) 
