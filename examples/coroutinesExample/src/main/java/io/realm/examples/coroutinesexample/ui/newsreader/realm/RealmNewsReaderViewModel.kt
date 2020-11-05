/*
 * Copyright 2020 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package io.realm.examples.coroutinesexample.ui.newsreader.realm

import android.util.Log
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.dropbox.android.external.store4.*
import io.realm.RealmConfiguration
import io.realm.examples.coroutinesexample.TAG
import io.realm.examples.coroutinesexample.data.newsreader.local.realm.RealmNYTDao
import io.realm.examples.coroutinesexample.data.newsreader.local.realm.RealmNYTDaoImpl
import io.realm.examples.coroutinesexample.data.newsreader.local.realm.RealmNYTimesArticle
import io.realm.examples.coroutinesexample.data.newsreader.local.realm.insertArticles
import io.realm.examples.coroutinesexample.data.newsreader.network.NYTimesApiClient
import io.realm.examples.coroutinesexample.data.newsreader.network.NYTimesApiClientImpl
import io.realm.examples.coroutinesexample.data.newsreader.network.model.NYTimesArticle
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.onEach
import kotlin.time.ExperimentalTime
import kotlin.time.seconds

/**
 * Realm implementation.
 */
@ExperimentalCoroutinesApi
@ExperimentalStoreApi
@ExperimentalTime
@FlowPreview
class RealmNewsReaderViewModel : ViewModel() {

    private val realmDao: RealmNYTDao = RealmNYTDaoImpl(RealmConfiguration.Builder().build())
    private val store: Store<String, List<RealmNYTimesArticle>>

    private val nytApiClient: NYTimesApiClient = NYTimesApiClientImpl()

    private val _newsReaderState = MutableLiveData<RealmNewsReaderState>()
    val newsReaderState: LiveData<RealmNewsReaderState>
        get() = _newsReaderState

    private var currentJob: Job? = null

    init {
        val fetcher: Fetcher<String, List<NYTimesArticle>> = Fetcher.of { apiSection ->
            nytApiClient.getTopStories(apiSection).results
        }

        val sourceOfTruth: SourceOfTruth<String, List<NYTimesArticle>, List<RealmNYTimesArticle>> = SourceOfTruth.of(
                reader = { apiSection ->
                    realmDao.getArticles(apiSection).map { articles ->
                        if (articles.isEmpty()) null
                        else articles
                    }
                },
                writer = { apiSection, articles ->
                    realmDao.insertArticles(apiSection, articles)
                },
                delete = { apiSection ->
                    realmDao.deleteArticles(apiSection)
                },
                deleteAll = {
                    realmDao.deleteAllArticles()
                }
        )

        val cachePolicy = MemoryPolicy.MemoryPolicyBuilder<String, List<RealmNYTimesArticle>>()
                .setMaxSize(3)
                .setExpireAfterWrite(30.seconds)
                .build()

        store = StoreBuilder.from(fetcher, sourceOfTruth)
                .cachePolicy(cachePolicy)
                .build()
    }

    override fun onCleared() {
        realmDao.close()
    }

    fun getTopStories(apiSection: String, refresh: Boolean = false) {
        Log.d(TAG, "--- apiSection: $apiSection - refresh '$refresh'")
        viewModelScope.launch {
            // Cancel previous streams when selecting a different section
            currentJob?.cancelAndJoin()
            currentJob = store.stream(StoreRequest.cached(
                    key = apiSection,
                    refresh = refresh
            )).onEach { response ->
                val origin = response.origin.toString()

                when (response) {
                    is StoreResponse.Loading -> {
                        Log.d(TAG, "--- response origin: ${response.origin} - Loading '$apiSection'")
                        RealmNewsReaderState.Loading(origin)
                    }
                    is StoreResponse.Data -> {
                        Log.d(TAG, "--- response origin: ${response.origin} - Data '$apiSection': ${response.value.size}")
                        RealmNewsReaderState.Data(origin, response.value)
                    }
                    is StoreResponse.NoNewData -> {
                        Log.d(TAG, "--- response origin: ${response.origin} - NoNewData '$apiSection'")
                        RealmNewsReaderState.NoNewData(origin)
                    }
                    is StoreResponse.Error.Exception -> {
                        Log.e(TAG, "--- response origin: ${response.origin} - Error.Exception '$apiSection': ${response.error}")
                        RealmNewsReaderState.ErrorException(origin, response.error)
                    }
                    is StoreResponse.Error.Message -> {
                        Log.e(TAG, "--- response origin: ${response.origin} - Error.Message '$apiSection': ${response.message}")
                        RealmNewsReaderState.ErrorMessage(origin, response.message)
                    }
                }.let {
                    _newsReaderState.postValue(it)
                }
            }.launchIn(viewModelScope)
        }
    }
}