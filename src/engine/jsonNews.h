
namespace jsonNews
{
    /**
     * A json news ticker
     * \defgroup json-news Json news ticker
     * \{
     */
   
    /**
     * A parsed news item
     */   
    struct NewsItem
    {
        /**
         * The title of a news item
         */
        const char *title;
       
        /**
         * The content of a news item
         */
        const char *content;
    };

    /**
     * The vector containing all the latest news items
     * \note Read only
     */
    extern vector<NewsItem *> newsItems;
   
    /**
     * Updates the ticker by making an http request
     */
    extern void update();
   
    /**
     * \}
     */
}