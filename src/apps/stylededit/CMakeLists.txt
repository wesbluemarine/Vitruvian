Application(
	StyledEdit

	SOURCES
	ColorMenuItem.cpp
	FindWindow.cpp
	ReplaceWindow.cpp
	StatusView.cpp
	StyledEditApp.cpp
	StyledEditView.cpp
	StyledEditWindow.cpp

	LIBS tracker shared translation translatorutils textencoding localestub
)

UsePrivateHeaders(StyledEdit textencoding tracker)
