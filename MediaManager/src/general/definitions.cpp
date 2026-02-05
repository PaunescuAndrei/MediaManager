#include "stdafx.h"
#include "definitions.h"
#include <QHash>

std::map<QString, int> ListColumns = {
	{"AUTHOR_COLUMN" , 0}, 
	{"NAME_COLUMN" , 1},
	{"PATH_COLUMN" , 2},
    {"BPM_COLUMN",3},
	{"TAGS_COLUMN" , 4},
	{"TYPE_COLUMN" , 5},
	{"WATCHED_COLUMN" , 6},
	{"VIEWS_COLUMN" , 7},
	{"RATING_COLUMN" , 8}, 
    {"RANDOM%_COLUMN" , 9},
    {"DATE_CREATED_COLUMN",10},
    {"LAST_WATCHED_COLUMN",11}
};
std::map<int, QString> ListColumns_reversed = {
    {0, "AUTHOR_COLUMN"}, 
    {1, "NAME_COLUMN"},
    {2, "PATH_COLUMN"},
    {3, "BPM_COLUMN"},
    {4, "TAGS_COLUMN"},
    {5, "TYPE_COLUMN"},
    {6,"WATCHED_COLUMN"}, 
    {7, "VIEWS_COLUMN"}, 
    {8, "RATING_COLUMN"},
    {9, "RANDOM%_COLUMN"},
    {10, "DATE_CREATED_COLUMN"},
    {11, "LAST_WATCHED_COLUMN"}
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
QStringList svTypes = QStringList();

 uint qHash(const Tag& key)
 {
	 return qHash(QPair<int, QPair<QString, int>>(key.id, qMakePair(key.name, key.display_priority)));
 }
