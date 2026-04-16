#pragma once

#if defined(__clang__)
#define KPBR_THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define KPBR_THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

#define CAPABILITY(x) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(capability(x))
#define SHARED_CAPABILITY(x) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(shared_capability(x))
#define SCOPED_CAPABILITY KPBR_THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)
#define ACQUIRE(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))
#define RELEASE(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))
#define ACQUIRE_SHARED(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))
#define RELEASE_SHARED(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))
#define GUARDED_BY(x) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
#define REQUIRES(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))
#define REQUIRES_SHARED(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))
#define EXCLUDES(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))
#define ACQUIRED_BEFORE(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))
#define ACQUIRED_AFTER(...) KPBR_THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))
