
SET NEW_USER_PATH=%CD%\freeglut 2.8.1\lib\x64;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\freeglut 2.8.1\lib\x86;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\glew-1.10.0\bin\Release\Win32;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\glew-1.10.0\bin\Release\x64;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\OpenCV 2.4.3\x64\vc10;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\OpenCV 2.4.3\x86\vc10;%NEW_USER_PATH%

SET NEW_USER_PATH=%CD%\OpenCV 2.4.8\x86\vc10\bin;%NEW_USER_PATH

SET NEW_USER_PATH=%CD%\OpenCV 2.4.8\x64\vc10\bin;%NEW_USER_PATH%
                                                   
setx PATH "%NEW_USER_PATH%"

PAUSE