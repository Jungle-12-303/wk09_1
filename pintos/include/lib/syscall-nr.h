#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* @lock
 * 시스템 콜 번호.
 */
enum {
	/* @lock
	 * 프로젝트 2 이상.
	 */
	/* @lock
	 * 운영체제를 종료한다.
	 */
	SYS_HALT,
	/* @lock
	 * 현재 프로세스를 종료한다.
	 */
	SYS_EXIT,
	/* @lock
	 * 현재 프로세스를 복제한다.
	 */
	SYS_FORK,
	/* @lock
	 * 현재 프로세스를 전환한다.
	 */
	SYS_EXEC,
	/* @lock
	 * 자식 프로세스가 죽을 때까지 기다린다.
	 */
	SYS_WAIT,
	/* @lock
	 * 파일을 생성한다.
	 */
	SYS_CREATE,
	/* @lock
	 * 파일을 삭제한다.
	 */
	SYS_REMOVE,
	/* @lock
	 * 파일을 연다.
	 */
	SYS_OPEN,
	/* @lock
	 * 파일의 크기를 얻는다.
	 */
	SYS_FILESIZE,
	/* @lock
	 * 파일에서 읽는다.
	 */
	SYS_READ,
	/* @lock
	 * 파일에 쓴다.
	 */
	SYS_WRITE,
	/* @lock
	 * 위치를 변경한다.
	 */
	SYS_SEEK,
	/* @lock
	 * 현재 위치를 보고한다.
	 */
	SYS_TELL,
	/* @lock
	 * 파일을 닫는다.
	 */
	SYS_CLOSE,

	/* @lock
	 * 프로젝트 3, 그리고 선택적으로 프로젝트 4.
	 */
	/* @lock
	 * 파일을 메모리에 매핑한다.
	 */
	SYS_MMAP,
	/* @lock
	 * 메모리 매핑을 제거한다.
	 */
	SYS_MUNMAP,

	/* @lock
	 * 프로젝트 4 전용.
	 */
	/* @lock
	 * 현재 디렉터리를 변경한다.
	 */
	SYS_CHDIR,
	/* @lock
	 * 디렉터리를 생성한다.
	 */
	SYS_MKDIR,
	/* @lock
	 * 디렉터리 엔트리를 읽는다.
	 */
	SYS_READDIR,
	/* @lock
	 * fd가 디렉터리를 나타내는지 검사한다.
	 */
	SYS_ISDIR,
	/* @lock
	 * fd에 대한 inode 번호를 반환한다.
	 */
	SYS_INUMBER,
	/* @lock
	 * fd에 대한 inode 번호를 반환한다.
	 */
	SYS_SYMLINK,

	/* @lock
	 * 프로젝트 2용 추가 요구사항.
	 */
	/* @lock
	 * 파일 디스크립터를 복제한다.
	 */
	SYS_DUP2,

	SYS_MOUNT,
	SYS_UMOUNT,
};

#endif /* lib/syscall-nr.h */
