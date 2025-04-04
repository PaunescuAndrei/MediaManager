#include "stdafx.h"
#include "definitions.h"
#include <QHash>

std::map<QString, int> ListColumns = {
	{"AUTHOR_COLUMN" , 0 }, 
	{"NAME_COLUMN" , 1 },
	{"PATH_COLUMN" , 2 },
	{"TAGS_COLUMN" , 3 },
	{"TYPE_COLUMN" , 4 },
	{"WATCHED_COLUMN" , 5},
	{"VIEWS_COLUMN" , 6},
	{"RATING_COLUMN" , 7}, 
	{"RANDOM%_COLUMN" , 8},
	{"DATE_CREATED_COLUMN",9},
	{"LAST_WATCHED_COLUMN",10}
};
std::map<int, QString> ListColumns_reversed = {
	{0, "AUTHOR_COLUMN"}, 
	{1, "NAME_COLUMN"},
	{2, "PATH_COLUMN"},
	{3, "TAGS_COLUMN"},
	{4, "TYPE_COLUMN" },
	{5,"WATCHED_COLUMN"}, 
	{6, "VIEWS_COLUMN"}, 
	{7, "RATING_COLUMN"},
	{8, "RANDOM%_COLUMN"},
	{9, "DATE_CREATED_COLUMN"},
	{10, "LAST_WATCHED_COLUMN"}
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

 uint qHash(const Tag& key)
 {
	 return qHash(QPair<int, QPair<QString, int>>(key.id, qMakePair(key.name, key.display_priority)));
 }
