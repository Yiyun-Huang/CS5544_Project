; ModuleID = 'test-pre.bc'
source_filename = "/work/tests/test.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@a = dso_local global ptr null, align 8
@b = dso_local global ptr null, align 8

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @main() #0 {
entry:
  %call = call noalias ptr @malloc(i64 noundef 20) #2
  store ptr %call, ptr @a, align 8
  %call1 = call noalias ptr @malloc(i64 noundef 20) #2
  store ptr %call1, ptr @b, align 8
  %0 = load ptr, ptr @b, align 8
  store ptr %0, ptr @a, align 8
  %call2 = call noalias ptr @malloc(i64 noundef 24) #2
  store ptr %call2, ptr @a, align 8
  %tobool = icmp ne i32 1, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr @b, align 8
  store ptr %1, ptr @a, align 8
  br label %if.end

if.else:                                          ; preds = %entry
  %call3 = call noalias ptr @malloc(i64 noundef 16) #2
  store ptr %call3, ptr @a, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  %2 = load ptr, ptr @a, align 8
  %3 = load i32, ptr %2, align 4
  ret i32 %3
}

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #1

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { nounwind allocsize(0) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 21.1.8 (https://github.com/llvm/llvm-project 2078da43e25a4623cab2d0d60decddf709aaea28)"}
