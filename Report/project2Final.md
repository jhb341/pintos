# CSED 312 Project 2 Final Report 
김지현(20220302) 
전현빈(20220259)

## Overall Project Design 
이번 프로젝트 2 에서는 아래의 네가지 tasks가 있다. 아래의 각각의 task를 실행시키기 위해 구현한 함수들을 설명하기 전에 전체적인 User Program 작동방식에 대해 설명하겠다. 부모와 자식 process 간의 관계를 저장하고 각 process 가 사용하는 resource (나중에 설명될 file 을 포함) 를 관리하기 위해서 pcb (process control block) 에 해당하는 구조체를 새롭게 정의하였다. 

```
/src/theads/thread.h
pcb structure
```

pcb 구조체 설명 (추가)

```
/src/theads/thread.h
file structure
```

이어서, 위에서 말한 각 processor가 사용하는 resource 중 하나인 file 을 관리하기 위해서 위와 같은 file 구조체를 정의하였다.  
file 구조체 설명 (추가)

```
/src/theads/thread.h
thread 구조에 어떻게 구현되어있는지
```

이처럼 위에서 설명한 pcb 구조체와 (기타 추가한 것들)을 각 thread 객체가 가질 수 있게 위와 같이 선언해준다. 이때, user program 을 실행시킬 때 위의 추가한 것들이 필요하기 때문에 #indef 를 사용해 userprog 매크로가 설정되었을 때만 생성하게 해주었다.  

```
/src/theads/thread.c
thread create 에서 어떻게 initialize 되어있는지 
```

그리고 위에서 설명했다시피 thread 구조체에서 정의된 새로운 객체들을 thread_create 함수에서 초기화해주었다. 
각 변수/객체들이 어떻게 초기화 되었는지 설명 (추가) 

## 1. Argument Passing 

### Implementation & Improvement from the previous design 
첫번째 과제는 argument passing 이다. argument 는 어떤 함수를 호출할 때 넘겨주는 인자로, 기존의 process_execute 함수를 보면 argument(인자)를 넘겨주지 않는 방식으로 구현되어있다. 하지만 pintos document 에 설명되었다시피 "grep foo bar" 이라는 명령어를 치면 "foo" 와 "bar" 이라는 인자를 넘겨주면서 "grep"이라는 함수를 호출해야한다. 따라서 이 명령어를 " "(공백)을 기준으로 parsing 해주어야한다. 

따라서, process_execute 함수에서 위의 명령어를 parsing 해주는 함수를 추가하였다. 
(그리고 process_exectue 함수에서 위에서 설명한 thread_create 함수를 호출하고 thread_create 의 인자중 하나로 start_process 가 넘겨지는데, 이 함수는 아래에서 설명할 것이다) 

```
/src/userprog/process.c & h 
argument parsing function 추가
```

위의 함수 설명 (추가) 

이때 pintos document에서 설명된 'lib/string.h' 파일에 있는 strtok_r 함수를 사용하여 구현하였다. 여기서 각 인자들은 char** argv 에 저장되며 int argc 는 인자의 총 갯수를 나타낸다. 

```
/src/userprog/process.c
start process function 
```

start_process 함수는 user program을 새 process 로 로드하고 실행하는 역할을 한다. 
start process 자체에서 수정한 내용 (추가) 
이때 프로그램을 실행할 때 필요한 인자를 user stack 에 추가해야하는데 이 역할을 하는 함수를 새롭게 추가하였다. 

```
user stack function
```

위의 함수를 보면 순서대로 user stack 에 값을 넣는 것을 볼 수 있다. 

user stack 내부 순서 사진 (추가) 

순서랑 왜 아래로 자라나게 만드는지 (추가) 

여기까지 구현하면 1. argument passing 에서 요구하는 목적을 달성했다. 

### Difference from design report 
```
process_execute 함수는 현재 "file_name"으로 문자열 전체를 받아오지만, 이 문자열을 파일 이름과 인자로 구분하는 과정이 필요하다. /src/lib/string.c의 strtok_r() 함수를 사용하여 공백으로 구분하여 파일 이름과 인자를 분리하고, start_process를 호출할 때 스택에 인자들을 추가한 후 스택을 전달하는 방식으로 구현할 예정이다. 이를 위해 파싱된 인자들을 스택에 추가하는 함수를 만들고, 이 함수를 start_process 함수에서 성공 시(if (success) {...}) 실행되도록 수정할 예정이다.
```

strtok_r 함수를 사용해서 구현한다는 것은 랩시간과 pintos document 에서도 설명되었기 때문에 어려움 없이 명령어 parsing 과정을 진행할 수 있었다. 하지만 start_process 함수에서 위에서 parsing 한 인자들과 인자 갯수, return address 등등 user stack 에 추가해야하는 함수들에 대한 구체적인 design 을 하지 않았다. 하지만 수업에서 배운 것 처럼 user stack 은 아래로 커지고 (address 적으로 설명 (추가) ) 어떤 순서로 값이 저장되어야하는지 알기 때문에 이를 바탕으로 구현을 하였다. 


## 2. System Calls 

### Implementation & Improvement from the previous design 
두번째 과제는 system call 을 구현하는 것이었다. system call 은 user program 이 OS 기능에 접근하기 위해 사용하는 함수들로 크게 두가지로 나눌 수 있다. process 작동에 대한 기능들과 파일 접근에 대한 기능이다. 
먼저 process 작동에 대한 기능으로는 halt, exit, exec, 그리고 wait 이 있고, 파일 접근에 대한 기능으로는 create, remove, open, filesize, read, write, seek, tell 그리고 close 가 있다. 

```
/src/userprog/syscall.c
halt
```

먼저 halt 는 pintos document 에서 나왔다시피 shutdown_power_off 함수를 호출하는 동작만 수행하면 된다. 
이 함수에 대한 설명과 언제 사용하게 되는지에 대한 설명 (추가) 

```
exit
```

exit 함수는 현재 실행중인 user program 을 종료하고 kernel 에 status 를 반환하는 함수이다. 만약 현재 process 의 부모 process 가 wait 중이라면 반환되는 것이 이 status 일 것이다. 즉 status 는 exit 코드와 같다. 
보통 0 은 현재 process 가 성공적으로 종료되었다는 것을 나타내고 0이 아니라면 error 가 발생했다는 것을 알 수 있다. 해당 함수는 thread_exit 함수 호출을 통해서 실제로 process 를 종료시킬 수 있다. 


```
exec
```
```wait```
```create```
```remove```
```open```
```filesize```
```read```
```write```
```seek```
```tell```



### Difference from design report 
```
현재 syscall_handler는 바로 종료하는 방식으로 구현되어 있으므로, 이 부분을 우선적으로 수정해야 한다. 우선 intr_frame의 esp 값을 읽어와, 스택에 사용자 프로그램이 push한 인자들과 시스템 호출 번호를 4바이트 단위로 읽어들여 이를 스택 또는 큐에 복사해 저장한다. 이후 시스템 호출에 필요한 정보를 syscall_handler에 저장하여 처리할 수 있도록 한다.

그 후 syscall_handler에 각 시스템 호출 번호에 해당하는 헬퍼 함수를 정의하고 구현하여, 각 번호에 맞는 시스템 호출이 적절한 헬퍼 함수에서 처리되도록 구현한다. 예를 들어, sys_exit_helper와 같은 헬퍼 함수를 통해 시스템 호출 번호와 인자를 전달하고, 반환된 값을 intr_frame의 eax에 저장할 수 있도록 한다. 이때 적절한 헬퍼함수의 구현은 아래 4번 file system관련해서 구현해야하는 함수와 중복된다. 예를들어 file creation과 관련된 syscall을 수행하는 함수의 경우 파일 시스템 함수를 그대로 사용하면 될것이다.

또한, 시스템 호출 및 실행 파일 쓰기 제한을 위한 pcb(프로세스 제어 블록) 구조체를 새롭게 정의하고자 한다. pcb에는 프로세스의 고유 ID, 부모 프로세스, 파일 시스템 접근을 위한 플래그 변수, 대기 처리를 위한 변수, 현재 실행 중인 파일이 포함되어야 한다. 그리고 thread 구조체에 이 pcb 포인터를 추가한 뒤, 프로세스가 생성되고 시작될 때(start_process 시점)에 해당 스레드의 pcb를 초기화할 예정이다. 여기서 추가된 현재 실행 중인 파일을 가리키는 포인터는 아래의 4번 과제에서 더 자세히 설명할 예정이다.
```


## 3. Process Termination Messages 

### Implementation & Improvement from the previous design 
위에서 설명한 exit 함수에 printf 추가 

### Difference from design report 
```
유저 프로세스가 종료될 때마다 printf ("%s: exit(%d)\n", process_name, exit_code); 코드를 추가하여 프로세스 종료 메시지를 출력해야 한다. 이를 위해, thread 구조체에 종료 메시지 출력 여부를 알려주는 boolean 변수를 추가할 예정이다. 새로운 프로세스가 process_execute 함수에서 생성될 경우 이 변수를 true로 설정하고, halt나 커널 스레드 종료 시에는 false로 설정한다. 이후 thread_exit 함수에서 해당 출력 명령을 수행하도록 한다.
```


## 4. Denying Writes to Executables 

### Implementation & Improvement from the previous design 


### Difference from design report 
```
현재 실행 중인 파일 각각에 대한 쓰기 허용/제한을 관리하는 방법으로 두 가지를 고려해 보았다. 첫 번째 방법은, 현재 실행 중인 파일들의 리스트를 만들어 각 파일의 실행이 끝날 때마다 리스트에서 제거하는 방식이다. 이 방법은 실행 중인 파일 목록을 직관적으로 파악할 수 있다는 장점이 있지만, 실행 종료 후 리스트에서 제거하거나 쓰기 작업을 위해 확인할 때 리스트의 모든 요소를 검사해야 하므로 오버헤드가 클 것으로 예상된다.

두 번째 방법은, 현재 디자인 분석에서 설명한 file_deny_write와 file_allow_write 함수를 활용하는 방식이다. 각 프로세스가 시작될 때 file_deny_write 함수를 호출하여 쓰기를 제한하고, 해당 프로세스가 종료되면 file_allow_write 함수를 호출하여 쓰기를 허용한다. 이번 프로젝트에서는 두 번째 방법을 이용하여 과제를 구현하고자 한다.

따라서, 현재 실행 중인 파일을 가리키는 포인터 변수를 위에서 생성한 process control block 구조체에 추가해준다. 이후 start_process에서 load 함수를 호출한 후, load 함수 내에서 이 포인터가 현재 실행 중인 파일을 가리키도록 설정하고 file_deny_write 함수를 호출하여 쓰기를 제한한다. 마지막으로, 작업이 완료되어 process_exit 함수가 호출될 때 file_allow_write 함수를 호출하여 다시 쓰기 작업을 허용함으로써 과제 4번을 구현할 수 있을 것이다.
```

### Overall Limitations 


### Overall Discussion 



# Result 
