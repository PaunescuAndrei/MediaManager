#include "stdafx.h"
#include "definitions.h"

std::map<QString, int> ListColumns = {
	{"AUTHOR_COLUMN" , 0 }, 
	{"NAME_COLUMN" , 1 },
	{"PATH_COLUMN" , 2 },
	{"TYPE_COLUMN" , 3 },
	{"WATCHED_COLUMN" , 4},
	{"VIEWS_COLUMN" , 5},
	{"RATING_COLUMN" , 6}, 
	{"DATE_CREATED_COLUMN",7},
	{"LAST_WATCHED_COLUMN",8}
};
std::map<int, QString> ListColumns_reversed = {
	{0, "AUTHOR_COLUMN"}, 
	{1, "NAME_COLUMN"},
	{2, "PATH_COLUMN"},
	{3, "TYPE_COLUMN" },
	{4,"WATCHED_COLUMN"}, 
	{5, "VIEWS_COLUMN"}, 
	{6, "RATING_COLUMN"},
	{7, "DATE_CREATED_COLUMN"},
	{8, "LAST_WATCHED_COLUMN"}
};
std::map<QString, int> sortingDict = { 
	{"Ascending" , Qt::SortOrder::AscendingOrder}, 
	{"Descending" , Qt::SortOrder::DescendingOrder } 
};
std::map<int, QString> sortingDict_reversed = {
	{Qt::SortOrder::AscendingOrder,"Ascending"}, 
	{Qt::SortOrder::DescendingOrder,"Descending" } 
};

QStringList videoTypes = QStringList();